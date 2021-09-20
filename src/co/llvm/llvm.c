#include "../common.h"
#include "../parse/parse.h"
#include "../util/rtimer.h"
#include "llvm.h"

#include <llvm-c/Transforms/AggressiveInstCombine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/OrcEE.h>

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
#define DEBUG_BUILD_EXPR

// rtimer helpers
#define ENABLE_RTIMER_LOGGING
#ifdef ENABLE_RTIMER_LOGGING
  #define RTIMER_INIT          RTimer rtimer_ = {0}
  #define RTIMER_START()       rtimer_start(&rtimer_)
  #define RTIMER_LOG(fmt, ...) rtimer_log(&rtimer_, fmt, ##__VA_ARGS__)
#else
  #define RTIMER_INIT          do{}while(0)
  #define RTIMER_START()       do{}while(0)
  #define RTIMER_LOG(fmt, ...) do{}while(0)
#endif


// make the code more readable by using short name aliases
typedef LLVMValueRef  Value;

// B is internal data used during IR construction
typedef struct B {
  Build*          build; // Co build (package, mem allocator, etc)
  LLVMContextRef  ctx;
  LLVMModuleRef   mod;
  LLVMBuilderRef  builder;

  // debug info
  bool prettyIR; // if true, include names in the IR (function params, variables, etc)
  //std::unique_ptr<DIBuilder>   DBuilder;
  //DebugInfo                    debug;
  bool noload; // for NVar

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // AST types, keyed by typeid
  SymMap internedTypes;

  // type constants
  LLVMTypeRef t_void;
  LLVMTypeRef t_bool;
  LLVMTypeRef t_i8;
  LLVMTypeRef t_i16;
  LLVMTypeRef t_i32;
  LLVMTypeRef t_i64;
  LLVMTypeRef t_f32;
  LLVMTypeRef t_f64;

  LLVMTypeRef t_int;
  LLVMTypeRef t_size;

} B;

typedef enum {
  Immutable,
  Mutable,
} Mutability;


__attribute__((used))
static const char* fmtvalue(LLVMValueRef v) {
  if (!v)
    return "(null)";
  static char* p[5] = {NULL};
  static u32 index = 0;
  u32 i = index++;
  if (index == countof(p))
    index = 0;
  if (p[i])
    LLVMDisposeMessage(p[i]);

  // avoid printing entire function bodies (just use its type)
  LLVMTypeRef ty = LLVMTypeOf(v);
  LLVMTypeKind tk = LLVMGetTypeKind(ty);
  while (tk == LLVMPointerTypeKind) {
    ty = LLVMGetElementType(ty);
    tk = LLVMGetTypeKind(ty);
  }
  if (tk == LLVMFunctionTypeKind) {
    p[i] = LLVMPrintTypeToString(ty);
  } else {
    p[i] = LLVMPrintValueToString(v);
  }

  return p[i];
}


__attribute__((used))
static const char* fmttype(LLVMTypeRef ty) {
  if (!ty)
    return "(null)";
  static char* p[5] = {NULL};
  static u32 index = 0;
  u32 i = index++;
  if (index == countof(p))
    index = 0;
  if (p[i])
    LLVMDisposeMessage(p[i]);
  p[i] = LLVMPrintTypeToString(ty);
  return p[i];
}


static LLVMTypeRef get_struct_type(B* b, Type* tn);


static LLVMTypeRef get_type(B* b, Type* nullable n) {
  if (!n)
    return b->t_void;
  switch (n->kind) {
    case NBasicType: {
      switch (n->t.basic.typeCode) {
        case TypeCode_bool:
          return b->t_bool;
        case TypeCode_i8:
        case TypeCode_u8:
          return b->t_i8;
        case TypeCode_i16:
        case TypeCode_u16:
          return b->t_i16;
        case TypeCode_i32:
        case TypeCode_u32:
          return b->t_i32;
        case TypeCode_i64:
        case TypeCode_u64:
          return b->t_i64;
        case TypeCode_f32:
          return b->t_f32;
        case TypeCode_f64:
          return b->t_f64;
        case TypeCode_ideal:
        case TypeCode_int:
        case TypeCode_uint:
          return b->t_int;
        case TypeCode_nil:
          return b->t_void;
        default: {
          panic("TODO basic type %s", n->t.basic.name);
          break;
        }
      }
      break;
    }
    case NStructType:
      return get_struct_type(b, n);
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }
  panic("invalid node kind %s", NodeKindName(n->kind));
  return NULL;
}


static bool value_is_ret(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMRet;
}

__attribute__((used))
static bool value_is_call(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMCall;
}

inline static LLVMBasicBlockRef get_current_block(B* b) {
  return LLVMGetInsertBlock(b->builder);
}

inline static Value get_current_fun(B* b) {
  return LLVMGetBasicBlockParent(get_current_block(b));
}


static Value build_expr(B* b, Node* n, const char* debugname);


// build_expr_noload calls build_expr with b->noload set to true, ignoring result value
inline static Value build_expr_noload(B* b, Node* n, const char* debugname) {
  bool noload = b->noload; // save
  b->noload = true;
  build_expr(b, n, debugname);
  b->noload = noload; // restore
  return n->irval;
}

inline static Value build_expr_mustload(B* b, Node* n, const char* debugname) {
  bool noload = b->noload; // save
  b->noload = false;
  Value v = build_expr(b, n, debugname);
  b->noload = noload; // restore
  return v;
}


inline static Sym ntypeid(B* b, Type* tn) {
  return tn->t.id ? tn->t.id : GetTypeID(b->build, tn);
}


static LLVMTypeRef nullable get_intern_type(B* b, Type* tn) {
  assert_debug(NodeIsType(tn));
  Sym tid = ntypeid(b, tn);
  return (LLVMTypeRef)SymMapGet(&b->internedTypes, tid);
}

static void add_intern_type(B* b, Type* tn, LLVMTypeRef tr) {
  assert_debug(NodeIsType(tn));
  assertnull_debug(get_intern_type(b, tn)); // must not be defined
  Sym tid = ntypeid(b, tn);
  SymMapSet(&b->internedTypes, tid, tr);
}


static LLVMTypeRef build_funtype(B* b, Node* nullable params, Node* nullable result) {
  LLVMTypeRef returnType = get_type(b, result);
  LLVMTypeRef* paramsv = NULL;
  u32 paramsc = 0;
  if (params) {
    asserteq(params->kind, NTupleType);
    paramsc = params->t.tuple.a.len;
    paramsv = memalloc(b->build->mem, sizeof(void*) * paramsc);
    for (u32 i = 0; i < paramsc; i++) {
      paramsv[i] = get_type(b, params->t.tuple.a.v[i]);
    }
  }
  auto ft = LLVMFunctionType(returnType, paramsv, paramsc, /*isVarArg*/false);
  if (paramsv)
    memfree(b->build->mem, paramsv);
  return ft;
}


static LLVMTypeRef get_funtype(B* b, Type* tn) {
  LLVMTypeRef tr = get_intern_type(b, tn);
  if (!tr) {
    tr = build_funtype(b, tn->t.fun.params, tn->t.fun.result);
    add_intern_type(b, tn, tr);
  }
  return tr;
}


static Value build_funproto(B* b, Node* n, const char* name) {
  asserteq(n->kind, NFun);
  LLVMTypeRef ft = get_funtype(b, n->type);
  // auto f = &n->fun;
  Value fn = LLVMAddFunction(b->mod, name, ft);

  // set argument names (for debugging)
  if (b->prettyIR && n->fun.params) {
    auto a = n->fun.params->array.a;
    for (u32 i = 0; i < a.len; i++) {
      auto param = (Node*)a.v[i];
      // param->kind==NArg
      Value p = LLVMGetParam(fn, i);
      LLVMSetValueName2(p, param->var.name, symlen(param->var.name));
    }
  }

  // linkage & visibility
  if (n->fun.name && strcmp(name, "main") != 0) {
    // TODO: only set for globals
    // Note on LLVMSetVisibility: visibility is different.
    // See https://llvm.org/docs/LangRef.html#visibility-styles
    // LLVMPrivateLinkage is like "static" in C but omit from symbol table
    LLVMSetLinkage(fn, LLVMPrivateLinkage);
    // LLVMSetLinkage(fn, LLVMInternalLinkage); // like "static" in C
  }

  return fn;
}


static Value build_fun(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFun);
  assertnotnull_debug(n->type);
  asserteq_debug(n->type->kind, NFunType);

  if (n->irval)
    return (Value)n->irval;

  auto f = &n->fun;

  LLVMValueRef fn; {
    const char* name = f->name;
    if (name == NULL || strcmp(name, "main") != 0)
      name = str_fmt("%s%s", name, n->type->t.id);
    fn = build_funproto(b, n, name);
    if (name != f->name)
      str_free((Str)name);
  }

  n->irval = fn;

  if (!n->fun.body) { // external
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
  }

  // save any current builder position
  LLVMBasicBlockRef prevb = get_current_block(b);

  // create a new basic block to start insertion into
  LLVMBasicBlockRef entryb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, entryb);

  // process params eagerly
  if (n->fun.params) {
    auto a = n->fun.params->array.a;
    for (u32 i = 0; i < a.len; i++) {
      auto pn = (Node*)a.v[i];
      asserteq_debug(pn->kind, NVar);
      assert_debug(NodeIsParam(pn));
      Value pv = LLVMGetParam(fn, i);
      if (NodeIsConst(pn)) { // immutable
        pn->irval = pv;
      } else { // mutable
        LLVMTypeRef ty = get_type(b, pn->type);
        pn->irval = LLVMBuildAlloca(b->builder, ty, pn->var.name);
        LLVMBuildStore(b->builder, pv, pn->irval);
      }
    }
  }

  // build body
  Value bodyval = build_expr(b, n->fun.body, "");

  // handle implicit return at end of body
  if (!bodyval || !value_is_ret(bodyval)) {
    if (!bodyval || n->type->t.fun.result == Type_nil) {
      LLVMBuildRetVoid(b->builder);
    } else {
      // if (value_is_call(bodyval)) {
      //   // TODO: might need to add a condition for matching parameters & return type
      //   LLVMSetTailCall(bodyval, true);
      // }
      LLVMBuildRet(b->builder, bodyval);
    }
  }

  // restore any current builder position
  if (prevb)
    LLVMPositionBuilderAtEnd(b->builder, prevb);

  return fn;
}


static Value build_block(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBlock);
  assertnotnull_debug(n->type);

  Value v = NULL; // return null to signal "empty block"
  for (u32 i = 0; i < n->array.a.len; i++) {
    v = build_expr(b, n->array.a.v[i], "");
  }
  // last expr of block is its value (TODO: is this true? is that Co's semantic?)
  return v;
}


static Value build_call(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NCall);
  assertnotnull_debug(n->type);

  if (NodeIsType(n->call.receiver)) {
    // type call, e.g. str(1)
    panic("TODO: type call");
    return NULL;
  }
  // n->call.receiver->kind==NFun
  Value callee = build_expr(b, n->call.receiver, "callee");
  if (!callee) {
    errlog("unknown function");
    return NULL;
  }

  // arguments
  Value* argv = NULL;
  u32 argc = 0;
  auto args = assertnotnull_debug(n->call.args);
  if (args != Const_nil) {
    asserteq(args->kind, NTuple);
    argc = args->array.a.len;
    argv = memalloc(b->build->mem, sizeof(void*) * argc);
    for (u32 i = 0; i < argc; i++) {
      argv[i] = build_expr(b, args->array.a.v[i], "arg");
    }
  }

  // check argument count
  #ifdef DEBUG
  if (LLVMCountParams(callee) != argc) {
    errlog("wrong number of arguments: %u (expected %u)", argc, LLVMCountParams(callee));
    return NULL;
  }
  #endif

  Value v = LLVMBuildCall(b->builder, callee, argv, argc, "");
  // LLVMSetTailCall(v, true); // set tail call when we know it for sure
  if (argv)
    memfree(b->build->mem, argv);
  return v;
}


static Value build_typecast(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NTypeCast);
  assertnotnull_debug(n->type);

  LLVMBool isSigned = false;
  LLVMTypeRef dsttype = b->t_i32;
  LLVMValueRef srcval = build_expr(b, n->call.args, "");
  return LLVMBuildIntCast2(b->builder, srcval, dsttype, isSigned, debugname);
}


static Value build_return(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NReturn);
  assertnotnull_debug(n->type);
  // TODO: check current function and if type is nil, use LLVMBuildRetVoid
  LLVMValueRef v = build_expr(b, n->op.left, debugname);
  // if (value_is_call(v))
  //   LLVMSetTailCall(v, true);
  return LLVMBuildRet(b->builder, v);
}


static LLVMTypeRef build_struct_type(B* b, Type* n) {
  asserteq_debug(n->kind, NStructType);

  u32 elemc = n->t.struc.a.len; // get_type
  LLVMTypeRef elemv_st[32];
  LLVMTypeRef* elemv = elemv_st; // TODO: memalloc if needed

  for (u32 i = 0; i < n->t.struc.a.len; i++) {
    Node* field = n->t.struc.a.v[i];
    asserteq_debug(field->kind, NField);
    elemv[i] = get_type(b, field->type);
  }

  LLVMTypeRef ty = LLVMStructCreateNamed(b->ctx, ntypeid(b, n));
  LLVMStructSetBody(ty, elemv, elemc, /*packed*/false);

  //return LLVMStructTypeInContext(b->ctx, elemv, elemc, /*packed*/false);
  return ty;
}


static LLVMTypeRef get_struct_type(B* b, Type* tn) {
  asserteq_debug(tn->kind, NStructType);
  LLVMTypeRef ty = get_intern_type(b, tn);
  if (!ty) {
    ty = build_struct_type(b, tn);
    add_intern_type(b, tn, ty);
  }
  return ty;
}


static Value build_struct_type_expr(B* b, Type* tn, const char* debugname) {
  // struct type used as value
  LLVMTypeRef ty = get_struct_type(b, tn);

  if (LLVMGetInsertBlock(b->builder)) // inside function
    return LLVMBuildAlloca(b->builder, ty, debugname);

  // global scope
  LLVMValueRef* vals = NULL; // must be const
  u32 nvals = 0;
  return LLVMConstStructInContext(b->ctx, vals, nvals, /*packed*/false);
}


static Value build_struct(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NStructCons);
  assertnotnull_debug(n->type);

  LLVMTypeRef ty = get_struct_type(b, n->type);

  if (LLVMGetInsertBlock(b->builder)) { // inside function
    dlog("TODO: initialize fields");
    return LLVMBuildAlloca(b->builder, ty, debugname);
  }

  // global scope (FIXME)
  LLVMValueRef* vals = NULL; // must be const
  u32 nvals = 0;
  return LLVMConstStructInContext(b->ctx, vals, nvals, /*packed*/false);
}


static Value build_selector(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NSelector);
  assertnotnull_debug(n->type);

  dlog("TODO: GEP");

  Value pointer = build_expr(b, n->sel.operand, debugname);
  LLVMTypeRef ty = get_type(b, n->type);

  dlog("do GEP");

  u32 field_index = 0; // fixme

  return LLVMBuildStructGEP2(b->builder, ty, pointer, field_index, debugname);

  // TODO: if struct is a constant (materialized w/ LLVMConstStructInContext)
  // then use LLVMConstGEP2.

  // return LLVMConstInt(b->t_int, 0, /*signext*/false); // placeholder
}


// loads the value of field at index from struct at memory location ptr
static Value build_struct_loadelem(B* b, Value ptr, u32 index, const char* debugname) {
  assertnotnull_debug(ptr);

  LLVMTypeRef st_ty = LLVMGetElementType(LLVMTypeOf(ptr));
  assert_debug(index < LLVMCountStructElementTypes(st_ty));

  LLVMValueRef indexv[2] = {
    LLVMConstInt(b->t_i32, 0, /*signext*/false),
    LLVMConstInt(b->t_i32, index, /*signext*/false),
  };

  // "inbounds" — the result value of the GEP is undefined if the address is outside
  // the actual underlying allocated object and not the address one-past-the-end.
  Value elemptr = LLVMBuildInBoundsGEP2(b->builder, st_ty, ptr, indexv, 2, debugname);

  LLVMTypeRef elem_ty = LLVMStructGetTypeAtIndex(st_ty, index);
  return LLVMBuildLoad2(b->builder, elem_ty, elemptr, debugname);
}


static Value build_index(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIndex);
  assertnotnull_debug(n->type);
  assertnotnull_debug(n->index.index);
  assertnotnull_debug(n->index.operand->type);

  Node* operand = assertnotnull_debug(n->index.operand);
  u64 index = n->index.index->val.i;

  // debugname "operand.index"
  #ifdef DEBUG
  char debugname2[256];
  if (debugname[0] == 0 && operand->kind == NVar) {
    int len = snprintf(
      debugname2, sizeof(debugname2), "%s.%llu", operand->var.name, index);
    if ((size_t)len >= sizeof(debugname2))
      debugname2[sizeof(debugname2) - 1] = '\0'; // truncate
    debugname = debugname2;
  }
  #endif

  switch (operand->type->kind) {
    case NTupleType: {
      asserteq_debug(n->index.index->kind, NIntLit); // must be resolved const
      Value ptr = assertnotnull_debug(build_expr_noload(b, operand, debugname));
      assert_debug(index <= 0xFFFFFFFF);
      return build_struct_loadelem(b, ptr, (u32)index, debugname);
    }

    // case NStructType:

    // case NArrayType:
    // TODO: LLVMBuildGEP2
    // LLVMValueRef LLVMBuildGEP2(LLVMBuilderRef B, LLVMTypeRef Ty,
    //                          LLVMValueRef Pointer, LLVMValueRef *Indices,
    //                          unsigned NumIndices, const char *Name);

    default:
      panic("TODO: %s", NodeKindName(operand->type->kind));
  }

  return LLVMConstInt(b->t_int, 0, /*signext*/false); // placeholder
}


static Value build_default_value(B* b, Type* tn) {
  LLVMTypeRef ty = get_type(b, tn);
  return LLVMConstNull(ty);
}


static Value load_var(B* b, Node* n, const char* debugname) {
  assert_debug(n->kind == NVar);
  Value v = (Value)n->irval;
  assertnotnull_debug(v);

  if (NodeIsConst(n) || b->noload) {
    // dlog(">> load_var use value (type %s): %s", fmttype(LLVMTypeOf(v)), fmtvalue(v));
    return v;
  }

  if (debugname[0] == 0)
    debugname = n->var.name;

  LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(v));
  // dlog(">> load_var load ptr (type %s => %s): %s",
  //   fmttype(LLVMTypeOf(v)), fmttype(ty), fmtvalue(v));
  return LLVMBuildLoad2(b->builder, ty, v, debugname);
}


static Value build_var_def(B* b, Node* n, const char* debugname, Value nullable init) {
  asserteq_debug(n->kind, NVar);
  assertnull_debug(n->irval);
  assert_debug( ! NodeIsParam(n)); // params are eagerly built by build_fun

  if (n->var.nrefs == 0 && !n->type) // skip unused var
    return NULL;

  assertnotnull_debug(n->type);

  if (debugname[0] == 0)
    debugname = n->var.name;

  bool noload = b->noload; // save
  b->noload = false;

  if (NodeIsConst(n)) {
    // immutable variable
    if (init) {
      n->irval = init;
    } else if (n->var.init) {
      n->irval = build_expr(b, n->var.init, debugname);
    } else {
      n->irval = build_default_value(b, n->type);
    }
  } else {
    // mutable variable
    // See https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl07.html
    LLVMTypeRef ty = get_type(b, n->type);
    n->irval = LLVMBuildAlloca(b->builder, ty, debugname);
    if (init || n->var.init) {
      if (!init)
        init = build_expr(b, n->var.init, debugname);
      LLVMBuildStore(b->builder, init, n->irval);
    }
  }

  b->noload = noload; // restore

  return (Value)n->irval;
}


static Value build_var(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NVar);

  // build var if needed
  if (!n->irval)
    build_var_def(b, n, debugname, NULL);

  R_MUSTTAIL return load_var(b, n, debugname);
}


static Value build_id_read(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NId);
  assertnotnull_debug(n->type);
  assertnotnull_debug(n->ref.target); // should be resolved
  Value target = build_expr(b, n->ref.target, n->ref.name);
  if (NodeIsConst(n->ref.target)) {
    // dlog(">> build_id_read use value (type %s): %s",
    //   fmttype(LLVMTypeOf(target)), fmtvalue(target));
    return target;
  }
  LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(target));
  // dlog(">> build_id_read load ptr (type %s => %s): %s",
  //   fmttype(LLVMTypeOf(target)), fmttype(ty), fmtvalue(target));
  return LLVMBuildLoad2(b->builder, ty, target, n->ref.name);
}


static Value build_assign_var(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->op.left->kind, NVar);

  const char* name = n->op.left->var.name;
  Value ptr = build_expr_noload(b, n->op.left, name);
  Value right = build_expr_mustload(b, n->op.right, "rvalue");
  LLVMBuildStore(b->builder, right, ptr);

  // value of assignment is its new value
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    LLVMTypeRef ty = LLVMGetElementType(LLVMTypeOf(ptr));
    return LLVMBuildLoad2(b->builder, ty, ptr, name);
  }

  return NULL;
}


static Value build_anon_struct(
  B* b, Value* values, u32 numvalues, const char* debugname, Mutability mut)
{
  u32 nconst = 0;
  for (u32 i = 0; i < numvalues; i++)
    nconst += LLVMIsConstant(values[i]);

  if (nconst == numvalues) {
    // all values are constant
    Value init = LLVMConstStructInContext(b->ctx, values, numvalues, /*packed*/false);
    if (mut == Mutable) {
      // struct will be modified; allocate on stack
      Value ptr = LLVMBuildAlloca(b->builder, LLVMTypeOf(init), debugname);
      LLVMBuildStore(b->builder, init, ptr);
      return ptr;
    } else {
      // struct is read-only; allocate as global
      LLVMValueRef ptr = LLVMAddGlobal(b->mod, LLVMTypeOf(init), debugname);
      LLVMSetLinkage(ptr, LLVMPrivateLinkage);
      LLVMSetInitializer(ptr, init);
      LLVMSetGlobalConstant(ptr, true);
      LLVMSetUnnamedAddr(ptr, true);

      // LLVMValueRef args[1];
      // args[0] = LLVMConstInt(b->t_i32, 0, false);
      // return LLVMConstInBoundsGEP(ptr, args, 1);
      return ptr;
    }
  }

  LLVMTypeRef typesv[32];
  LLVMTypeRef* types = typesv;
  if (R_UNLIKELY(countof(typesv) < numvalues))
    types = memalloc(b->build->mem, numvalues * sizeof(LLVMTypeRef));

  for (u32 i = 0; i < numvalues; i++)
    types[i] = LLVMTypeOf(values[i]);

  LLVMTypeRef ty = LLVMStructTypeInContext(b->ctx, types, numvalues, /*packed*/false);
  Value ptr = LLVMBuildAlloca(b->builder, ty, debugname);

  if (types != typesv)
    memfree(b->build->mem, types);

  for (u32 i = 0; i < numvalues; i++) {
    LLVMValueRef fieldptr = LLVMBuildStructGEP2(b->builder, ty, ptr, i, "");
    LLVMBuildStore(b->builder, values[i], fieldptr);
  }

  // return LLVMBuildLoad2(b->builder, ty, ptr, debugname);
  return ptr;
}


static Value build_assign_tuple(B* b, Node* n, const char* debugname) {
  Node* targets = n->op.left;
  Node* sources = n->op.right;
  asserteq_debug(targets->kind, NTuple);
  asserteq_debug(sources->kind, NTuple);
  asserteq_debug(targets->array.a.len, sources->array.a.len);

  Value srcvalsv[32];
  Value* srcvals = srcvalsv;
  if (R_UNLIKELY(countof(srcvalsv) < sources->array.a.len))
    srcvals = memalloc(b->build->mem, sources->array.a.len * sizeof(Value));

  // first load all sources in case a source var is in targets
  for (u32 i = 0; i < sources->array.a.len; i++) {
    Node* srcn = sources->array.a.v[i];
    Node* dstn = targets->array.a.v[i];
    if (srcn) {
      srcvals[i] = build_expr_mustload(b, srcn, "");
    } else {
      // variable definition
      build_var_def(b, dstn, dstn->var.name, NULL);
      srcvals[i] = load_var(b, dstn, dstn->var.name);
    }
    assertnotnull_debug(srcvals[i]);
  }

  // now store
  for (u32 i = 0; i < sources->array.a.len; i++) {
    Node* srcn = sources->array.a.v[i];
    Node* dstn = targets->array.a.v[i];

    if (srcn) {
      // assignment to existing memory location
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      Value ptr = assertnotnull_debug(build_expr_noload(b, dstn, dstn->var.name));
      LLVMBuildStore(b->builder, srcvals[i], ptr);
    }
  }

  Value result = NULL;

  // if the assignment is used as a value, make tuple val
  if ((n->flags & NodeFlagRValue) && !b->noload) {
    for (u32 i = 0; i < sources->array.a.len; i++) {
      Node* dstn = targets->array.a.v[i];
      if (dstn->kind != NVar)
        panic("TODO: dstn %s", NodeKindName(dstn->kind));
      srcvals[i] = load_var(b, dstn, dstn->var.name);
    }
    result = build_anon_struct(b, srcvals, sources->array.a.len, debugname, Immutable);
  }

  if (srcvals != srcvalsv)
    memfree(b->build->mem, srcvals);

  return result;
}


static Value build_assign(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NAssign);
  assertnotnull_debug(n->type);

  switch (n->op.left->kind) {
    case NVar:   R_MUSTTAIL return build_assign_var(b, n, debugname);
    case NTuple: R_MUSTTAIL return build_assign_tuple(b, n, debugname);
    default:
      panic("TODO assign to %s", NodeKindName(n->op.left->kind));
      return NULL;
  }
}


static Value build_binop(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NBinOp);
  assertnotnull_debug(n->type);

  Type* tn = n->op.left->type;
  asserteq_debug(tn->kind, NBasicType);
  assert_debug(tn->t.basic.typeCode < TypeCode_CONCRETE_END);
  assert_debug(n->op.op < T_PRIM_OPS_END);

  Value left = build_expr(b, n->op.left, "");
  Value right = build_expr(b, n->op.right, "");
  u32 op = 0;

  // signed integer binary operators
  static const u32 kOpTableSInt[T_PRIM_OPS_END] = {
    // op is LLVMOpcode
    [TPlus]    = LLVMAdd,    // +
    [TMinus]   = LLVMSub,    // -
    [TStar]    = LLVMMul,    // *
    [TSlash]   = LLVMSDiv,   // /
    [TPercent] = LLVMSRem,   // %
    [TShl]     = LLVMShl,    // <<
    // The shift operators implement arithmetic shifts if the left operand
    // is a signed integer and logical shifts if it is an unsigned integer.
    [TShr]     = LLVMAShr,   // >>
    [TAnd]     = LLVMAnd,    // &
    [TPipe]    = LLVMOr,     // |
    [THat]     = LLVMXor,    // ^
    // op is LLVMIntPredicate
    [TEq]      = LLVMIntEQ,  // ==
    [TNEq]     = LLVMIntNE,  // !=
    [TLt]      = LLVMIntSLT, // <
    [TLEq]     = LLVMIntSLE, // <=
    [TGt]      = LLVMIntSGT, // >
    [TGEq]     = LLVMIntSGE, // >=
  };

  // unsigned integer binary operators
  static const u32 kOpTableUInt[T_PRIM_OPS_END] = {
    // op is LLVMOpcode
    [TPlus]    = LLVMAdd,    // +
    [TMinus]   = LLVMSub,    // -
    [TStar]    = LLVMMul,    // *
    [TSlash]   = LLVMUDiv,   // /
    [TPercent] = LLVMURem,   // %
    [TShl]     = LLVMShl,    // <<
    [TShr]     = LLVMLShr,   // >>
    [TAnd]     = LLVMAnd,    // &
    [TPipe]    = LLVMOr,     // |
    [THat]     = LLVMXor,    // ^
    // op is LLVMIntPredicate
    [TEq]      = LLVMIntEQ,  // ==
    [TNEq]     = LLVMIntNE,  // !=
    [TLt]      = LLVMIntULT, // <
    [TLEq]     = LLVMIntULE, // <=
    [TGt]      = LLVMIntUGT, // >
    [TGEq]     = LLVMIntUGE, // >=
  };

  // floating-point number binary operators
  static const u32 kOpTableFloat[T_PRIM_OPS_END] = {
    // op is LLVMOpcode
    [TPlus]    = LLVMFAdd,  // +
    [TMinus]   = LLVMFSub,  // -
    [TStar]    = LLVMFMul,  // *
    [TSlash]   = LLVMFDiv,  // /
    [TPercent] = LLVMFRem,  // %
    // op is LLVMRealPredicate
    [TEq]      = LLVMRealOEQ, // ==
    [TNEq]     = LLVMRealUNE, // != (true if unordered or not equal)
    [TLt]      = LLVMRealOLT, // <
    [TLEq]     = LLVMRealOLE, // <=
    [TGt]      = LLVMRealOGT, // >
    [TGEq]     = LLVMRealOGE, // >=
  };

  bool isfloat = false;

  switch (tn->t.basic.typeCode) {
  case TypeCode_bool:
    switch (n->op.op) {
    case TEq:  op = LLVMIntEQ; break; // ==
    case TNEq: op = LLVMIntNE; break; // !=
    default: break;
    }
    break;
  case TypeCode_i8:
  case TypeCode_i16:
  case TypeCode_i32:
  case TypeCode_i64:
  case TypeCode_int:
  case TypeCode_isize:
    op = kOpTableSInt[n->op.op];
    break;
  case TypeCode_u8:
  case TypeCode_u16:
  case TypeCode_u32:
  case TypeCode_u64:
  case TypeCode_uint:
  case TypeCode_usize:
    op = kOpTableUInt[n->op.op];
    break;
  case TypeCode_f32:
  case TypeCode_f64:
    isfloat = true;
    op = kOpTableFloat[n->op.op];
    break;
  default:
    break;
  }

  if (op == 0) {
    build_errf(b->build, NodePosSpan(n), "invalid operand type %s", fmtnode(tn));
    return NULL;
  }

  if (n->op.op >= TEq && n->op.op <= TGEq) {
    // See how Go compares values: https://golang.org/ref/spec#Comparison_operators
    if (isfloat)
      return LLVMBuildFCmp(b->builder, (LLVMRealPredicate)op, left, right, debugname);
    return LLVMBuildICmp(b->builder, (LLVMIntPredicate)op, left, right, debugname);
  }
  return LLVMBuildBinOp(b->builder, (LLVMOpcode)op, left, right, debugname);
}


static Value build_if(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIf);
  assertnotnull_debug(n->type);

  bool isrvalue = (n->flags & NodeFlagRValue) && !b->noload;

  // condition
  assertnotnull_debug(n->cond.cond->type);
  asserteq_debug(n->cond.cond->type->kind, NBasicType);
  asserteq_debug(get_type(b, n->cond.cond->type), b->t_bool);
  Value condExpr = build_expr(b, n->cond.cond, "if.cond");

  Value fn = get_current_fun(b);

  LLVMBasicBlockRef thenb = LLVMAppendBasicBlockInContext(b->ctx, fn, "if.then");
  LLVMBasicBlockRef elseb = NULL;
  if (n->cond.elseb || isrvalue)
    elseb = LLVMCreateBasicBlockInContext(b->ctx, "if.else");
  LLVMBasicBlockRef endb = LLVMCreateBasicBlockInContext(b->ctx, "if.end");

  LLVMBuildCondBr(b->builder, condExpr, thenb, elseb ? elseb : endb);

  // then
  LLVMPositionBuilderAtEnd(b->builder, thenb);
  Value thenVal = build_expr(b, n->cond.thenb, "");
  LLVMBuildBr(b->builder, endb);
  // Codegen of "then" can change the current block, update thenb for the PHI
  thenb = LLVMGetInsertBlock(b->builder);

  // else
  Value elseVal = NULL;
  if (elseb) {
    LLVMAppendExistingBasicBlock(fn, elseb);
    LLVMPositionBuilderAtEnd(b->builder, elseb);
    if (n->cond.elseb) {
      if (!TypeEquals(b->build, n->cond.thenb->type, n->cond.elseb->type))
        panic("TODO: mixed types");
      elseVal = build_expr(b, n->cond.elseb, "");
    } else {
      elseVal = build_default_value(b, n->cond.thenb->type);
    }
    LLVMBuildBr(b->builder, endb);
    // Codegen of "then" can change the current block, update thenb for the PHI
    elseb = LLVMGetInsertBlock(b->builder);
  }

  // end
  LLVMAppendExistingBasicBlock(fn, endb);
  LLVMPositionBuilderAtEnd(b->builder, endb);

  if (!isrvalue) // "if" is used as a statement
    return NULL;

  // result type of if expression
  LLVMTypeRef ty = LLVMTypeOf(thenVal);
  Value phi = LLVMBuildPhi(b->builder, ty, ty == b->t_void ? "" : "if");
  Value             incomingValues[2] = { thenVal, elseVal };
  LLVMBasicBlockRef incomingBlocks[2] = { thenb,   elseb };
  LLVMAddIncoming(phi, incomingValues, incomingBlocks, 2);

  return phi;
}


static Value build_intlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NIntLit);
  assertnotnull_debug(n->type);
  return LLVMConstInt(get_type(b, n->type), n->val.i, /*signext*/false);
}


static Value build_floatlit(B* b, Node* n, const char* debugname) {
  asserteq_debug(n->kind, NFloatLit);
  assertnotnull_debug(n->type);
  return LLVMConstReal(get_type(b, n->type), n->val.f);
}


static Value build_expr(B* b, Node* n, const char* debugname) {

  #ifdef DEBUG_BUILD_EXPR
    static int indent = -1;
    static char spaces[256];
    if (indent == -1) {
      indent = 0;
      memset(spaces, ' ', sizeof(spaces));
    }
    if (debugname && debugname[0]) {
      dlog("%.*s• %s %s <%s> (\"%s\")",
        indent * 2, spaces,
        NodeKindName(n->kind), fmtnode(n), fmtnode(n->type), debugname);
    } else {
      dlog("%.*s• %s %s <%s>",
        indent * 2, spaces,
        NodeKindName(n->kind), fmtnode(n), fmtnode(n->type));
    }
    indent++;
    #define RET(x) { \
      Value v = x; \
      indent--; \
      dlog("%.*s  ↳ %s %s => %s", \
        indent * 2, spaces, NodeKindName(n->kind), fmtnode(n), v ? fmtvalue(v) : "void"); \
      return v; }
  #else
    #define RET(x) R_MUSTTAIL return x
  #endif

  switch (n->kind) {
    case NBinOp:      RET(build_binop(b, n, debugname));
    case NId:         RET(build_id_read(b, n, debugname));
    case NVar:        RET(build_var(b, n, debugname));
    case NIntLit:     RET(build_intlit(b, n, debugname));
    case NFloatLit:   RET(build_floatlit(b, n, debugname));
    case NBlock:      RET(build_block(b, n, debugname));
    case NCall:       RET(build_call(b, n, debugname));
    case NTypeCast:   RET(build_typecast(b, n, debugname));
    case NReturn:     RET(build_return(b, n, debugname));
    case NStructType: RET(build_struct_type_expr(b, n, debugname));
    case NStructCons: RET(build_struct(b, n, debugname));
    case NSelector:   RET(build_selector(b, n, debugname));
    case NIndex:      RET(build_index(b, n, debugname));
    case NFun:        RET(build_fun(b, n, debugname));
    case NAssign:     RET(build_assign(b, n, debugname));
    case NIf:         RET(build_if(b, n, debugname));
    default:
      panic("TODO node kind %s", NodeKindName(n->kind));
      break;
  }
  panic("invalid node kind %s", NodeKindName(n->kind));
  indent--;
  return NULL;
}


static Value build_global_let(B* b, Node* n) {
  assert(n->kind == NVar);
  assert(n->type);
  Value gv;
  if (n->var.init) {
    Value v = build_expr(b, n->var.init, n->var.name);
    if (!LLVMIsConstant(v)) {
      panic("not a constant expression %s", fmtnode(n));
    }
    gv = LLVMAddGlobal(b->mod, LLVMTypeOf(v), n->var.name);
    LLVMSetInitializer(gv, v);
  } else {
    gv = LLVMAddGlobal(b->mod, get_type(b, n->type), n->var.name);
  }
  n->irval = gv; // save pointer for later lookups
  // Note: global vars are always stored to after they are defined as
  // "x = y" becomes a variable definition if "x" is not yet defined.
  // TODO: conditionally make linkage private
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  return gv;
}


static void build_file(B* b, Node* n) {
  asserteq(n->kind, NFile);

  // TODO: set cat(filenames) instead of just the last file
  LLVMSetSourceFileName(b->mod, n->cunit.name, (size_t)str_len(n->cunit.name));

  // first build all globals
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    if (cn->kind == NVar)
      build_global_let(b, cn);
  }

  // then functions
  for (u32 i = 0; i < n->cunit.a.len; i++) {
    auto cn = (Node*)n->cunit.a.v[i];
    switch (cn->kind) {
      case NFun:
        assertnotnull(cn->fun.name);
        build_fun(b, cn, cn->fun.name);
        break;
      case NVar:
        break;
      default:
        dlog("TODO: %s", NodeKindName(cn->kind));
        break;
    }
  }
}


static void build_module(Build* build, Node* pkgnode, LLVMModuleRef mod) {
  LLVMContextRef ctx = LLVMGetModuleContext(mod);
  B _b = {
    .build = build,
    .ctx = ctx,
    .mod = mod,
    .builder = LLVMCreateBuilderInContext(ctx),
    .prettyIR = true,

    // FPM: Apply per-function optimizations. Set to NULL to disable.
    // Really only useful for JIT; for assembly to asm, obj or bc we apply module-wide opt.
    // .FPM = LLVMCreateFunctionPassManagerForModule(mod),
    .FPM = NULL,

    // constants
    // note: no disposal needed of built-in types
    .t_void = LLVMVoidTypeInContext(ctx),
    .t_bool = LLVMInt1TypeInContext(ctx),
    .t_i8 = LLVMInt8TypeInContext(ctx),
    .t_i16 = LLVMInt16TypeInContext(ctx),
    .t_i32 = LLVMInt32TypeInContext(ctx),
    .t_i64 = LLVMInt64TypeInContext(ctx),
    .t_f32 = LLVMFloatTypeInContext(ctx),
    .t_f64 = LLVMDoubleTypeInContext(ctx),
  };
  _b.t_int = _b.t_i32; // alias int = i32
  _b.t_size = _b.t_i64; // alias size = i32
  B* b = &_b;
  SymMapInit(&b->internedTypes, 16, build->mem);

  // initialize function pass manager (optimize)
  if (b->FPM) {
    // add optimization passes
    LLVMAddInstructionCombiningPass(b->FPM);
    LLVMAddReassociatePass(b->FPM);
    LLVMAddDCEPass(b->FPM);
    LLVMAddGVNPass(b->FPM);
    LLVMAddCFGSimplificationPass(b->FPM);
    // initialize FPM
    LLVMInitializeFunctionPassManager(b->FPM);
  }

  // build package parts
  for (u32 i = 0; i < pkgnode->cunit.a.len; i++) {
    auto cn = (Node*)pkgnode->cunit.a.v[i];
    build_file(b, cn);
  }

  // // build demo functions
  // build_fun1(b, "foo");
  // build_fun1(b, "main");

  // verify IR
  #ifdef DEBUG
    char* errmsg;
    bool ok = LLVMVerifyModule(b->mod, LLVMPrintMessageAction, &errmsg) == 0;
    if (!ok) {
      //errlog("=========== LLVMVerifyModule ===========\n%s\n", errmsg);
      LLVMDisposeMessage(errmsg);
      dlog("\n=========== LLVMDumpModule ===========");
      LLVMDumpModule(b->mod);
      goto finish;
    }
  #endif

  // finalize all function passes scheduled in the function pass
  if (b->FPM)
    LLVMFinalizeFunctionPassManager(b->FPM);

  #ifdef DEBUG
  dlog("LLVM IR module as built:");
  LLVMDumpModule(b->mod);
  #endif

#ifdef DEBUG
finish:
#endif
  SymMapDispose(&b->internedTypes);
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
}


static LLVMTargetRef select_target(const char* triple) {
  // select target
  char* errmsg;
  LLVMTargetRef target;
  if (LLVMGetTargetFromTriple(triple, &target, &errmsg) != 0) {
    // error
    errlog("LLVMGetTargetFromTriple: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    target = NULL;
  } else {
    #if DEBUG
    const char* name = LLVMGetTargetName(target);
    const char* description = LLVMGetTargetDescription(target);
    const char* jit = LLVMTargetHasJIT(target) ? " jit" : "";
    const char* mc = LLVMTargetHasTargetMachine(target) ? " mc" : "";
    const char* _asm = LLVMTargetHasAsmBackend(target) ? " asm" : "";
    dlog("selected target: %s (%s) [abilities:%s%s%s]", name, description, jit, mc, _asm);
    #endif
  }
  return target;
}


static LLVMTargetMachineRef select_target_machine(
  LLVMTargetRef       target,
  const char*         triple,
  LLVMCodeGenOptLevel optLevel,
  LLVMCodeModel       codeModel)
{
  if (!target)
    return NULL;

  const char* CPU = "";      // "" for generic
  const char* features = ""; // "" for none

  // select host CPU and features (NOT PORTABLE!) when optimizing
  char* hostCPUName = NULL;
  char* hostFeatures = NULL;
  if (optLevel != LLVMCodeGenLevelNone) {
    hostCPUName = LLVMGetHostCPUName();
    hostFeatures = LLVMGetHostCPUFeatures();
    CPU = hostCPUName;
    features = hostFeatures;
  }

  LLVMTargetMachineRef targetMachine = LLVMCreateTargetMachine(
    target, triple, CPU, features, optLevel, LLVMRelocStatic, codeModel);
  if (!targetMachine) {
    errlog("LLVMCreateTargetMachine failed");
    return NULL;
  } else {
    char* triple1 = LLVMGetTargetMachineTriple(targetMachine);
    dlog("selected target machine: %s", triple1);
    LLVMDisposeMessage(triple1);
  }
  if (hostCPUName) {
    LLVMDisposeMessage(hostCPUName);
    LLVMDisposeMessage(hostFeatures);
  }
  return targetMachine;
}


static LLVMOrcThreadSafeModuleRef llvm_jit_buildmod(Build* build, Node* pkgnode) {
  RTIMER_INIT;

  LLVMOrcThreadSafeContextRef tsctx = LLVMOrcCreateNewThreadSafeContext();
  LLVMContextRef ctx = LLVMOrcThreadSafeContextGetContext(tsctx);
  LLVMModuleRef M = LLVMModuleCreateWithNameInContext(build->pkg->id, ctx);

  // build module; Co AST -> LLVM IR
  // TODO: consider moving the IR building code to C++
  RTIMER_START();
  build_module(build, pkgnode, M);
  RTIMER_LOG("build llvm IR");

  // Wrap the module and our ThreadSafeContext in a ThreadSafeModule.
  // Dispose of our local ThreadSafeContext value.
  // The underlying LLVMContext will be kept alive by our ThreadSafeModule, TSM.
  LLVMOrcThreadSafeModuleRef TSM = LLVMOrcCreateNewThreadSafeModule(M, tsctx);
  LLVMOrcDisposeThreadSafeContext(tsctx);
  return TSM;
}


static int llvm_jit_handle_err(LLVMErrorRef Err) {
  char* errmsg = LLVMGetErrorMessage(Err);
  fprintf(stderr, "LLVM JIT error: %s\n", errmsg);
  LLVMDisposeErrorMessage(errmsg);
  return 1;
}


int llvm_jit(Build* build, Node* pkgnode) {
  dlog("llvm_jit");
  RTIMER_INIT;
  // TODO: see llvm/examples/OrcV2Examples/LLJITWithObjectCache/LLJITWithObjectCache.cpp
  // for an example of caching compiled code objects, like LLVM IR modules.

  int main_result = 0;
  LLVMErrorRef err;

  RTIMER_START();

  // Initialize native target codegen and asm printer
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmPrinter();

  // Create the JIT instance
  LLVMOrcLLJITRef J;
  if ((err = LLVMOrcCreateLLJIT(&J, 0))) {
    main_result = llvm_jit_handle_err(err);
    goto llvm_shutdown;
  }
  RTIMER_LOG("llvm JIT init");


  // build module
  LLVMOrcThreadSafeModuleRef M = llvm_jit_buildmod(build, pkgnode);
  LLVMOrcResourceTrackerRef RT;


  // // get execution session
  // LLVMOrcExecutionSessionRef ES = LLVMOrcLLJITGetExecutionSession(J);
  // LLVMOrcObjectLayerRef objlayer =
  //   LLVMOrcCreateRTDyldObjectLinkingLayerWithSectionMemoryManager(ES);


  // Add our module to the JIT
  LLVMOrcJITDylibRef MainJD = LLVMOrcLLJITGetMainJITDylib(J);
    RT = LLVMOrcJITDylibCreateResourceTracker(MainJD);
  if ((err = LLVMOrcLLJITAddLLVMIRModuleWithRT(J, RT, M))) {
    // If adding the ThreadSafeModule fails then we need to clean it up
    // ourselves. If adding it succeeds the JIT will manage the memory.
    LLVMOrcDisposeThreadSafeModule(M);
    main_result = llvm_jit_handle_err(err);
    goto jit_cleanup;
  }

  // Look up the address of our entry point
  RTIMER_START();
  LLVMOrcJITTargetAddress entry_addr;
  if ((err = LLVMOrcLLJITLookup(J, &entry_addr, "main"))) {
    main_result = llvm_jit_handle_err(err);
    goto mod_cleanup;
  }
  RTIMER_LOG("llvm JIT lookup entry function \"main\"");


  // If we made it here then everything succeeded. Execute our JIT'd code.
  RTIMER_START();
  auto entry_fun = (int(*)(void))entry_addr;
  int result = entry_fun();
  RTIMER_LOG("llvm JIT execute module main fun");
  fprintf(stderr, "main => %i\n", result);


  RTIMER_START();

mod_cleanup:
  // Remove the code
  if ((err = LLVMOrcResourceTrackerRemove(RT))) {
    main_result = llvm_jit_handle_err(err);
    goto jit_cleanup;
  }

  // Attempt a second lookup — we expect an error as the code & symbols have been removed
  #if DEBUG
  LLVMOrcJITTargetAddress tmp;
  if ((err = LLVMOrcLLJITLookup(J, &tmp, "main")) != 0) {
    // expect error
    LLVMDisposeErrorMessage(LLVMGetErrorMessage(err)); // must release error message
  } else {
    assert(err != 0); // expected error
  }
  #endif

jit_cleanup:
  // Destroy our JIT instance. This will clean up any memory that the JIT has
  // taken ownership of. This operation is non-trivial (e.g. it may need to
  // JIT static destructors) and may also fail. In that case we want to render
  // the error to stderr, but not overwrite any existing return value.
  LLVMOrcReleaseResourceTracker(RT);
  if ((err = LLVMOrcDisposeLLJIT(J))) {
    int x = llvm_jit_handle_err(err);
    if (main_result == 0)
      main_result = x;
  }
  // LLVMOrcDisposeObjectLayer(objlayer);

llvm_shutdown:
  // Shut down LLVM.
  LLVMShutdown();
  RTIMER_LOG("llvm JIT cleanup");
  return main_result;
}


bool llvm_build_and_emit(Build* build, Node* pkgnode, const char* triple) {
  dlog("llvm_build_and_emit");
  bool ok = false;
  RTIMER_INIT;

  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(build->pkg->id, ctx);


  // build module; Co AST -> LLVM IR
  // TODO: move the IR building code to C++
  RTIMER_START();
  build_module(build, pkgnode, mod);
  RTIMER_LOG("build llvm IR");


  // select target and emit machine code
  RTIMER_START();
  const char* hostTriple = llvm_init_targets();
  if (!triple)
    triple = hostTriple; // default to host
  LLVMTargetRef target = select_target(triple);
  LLVMCodeGenOptLevel optLevel =
    (build->opt == CoOptNone ? LLVMCodeGenLevelNone : LLVMCodeGenLevelDefault);
  LLVMCodeModel codeModel =
    (build->opt == CoOptSmall ? LLVMCodeModelSmall : LLVMCodeModelDefault);

  // optLevel = LLVMCodeGenLevelAggressive;
  LLVMTargetMachineRef targetm = select_target_machine(
    target, triple, optLevel, codeModel);
  if (!targetm)
    goto end;

  // set target
  LLVMSetTarget(mod, triple);
  LLVMTargetDataRef dataLayout = LLVMCreateTargetDataLayout(targetm);
  LLVMSetModuleDataLayout(mod, dataLayout);
  RTIMER_LOG("select llvm target");


  char* errmsg;

  // verify, optimize and target-fit module
  RTIMER_START();
  bool enable_tsan = false;
  bool enable_lto = false;
  if (!llvm_optmod(mod, targetm, build->opt, enable_tsan, enable_lto, &errmsg)) {
    errlog("llvm_optmod: %s", errmsg);
    LLVMDisposeMessage(errmsg);
    goto end;
  }
  RTIMER_LOG("llvm optimize module");
  #ifdef DEBUG
  dlog("LLVM IR module after target-fit and optimizations:");
  LLVMDumpModule(mod);
  #endif


  // emit
  const char* obj_file = "out1.o";
  const char* asm_file = "out1.asm";
  const char* bc_file  = "out1.bc";
  const char* ir_file  = "out1.ll";
  const char* exe_file = "out1.exe";

  // emit machine code (object)
  if (obj_file) {
    RTIMER_START();
    if (!llvm_emit_mc(mod, targetm, LLVMObjectFile, obj_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMObjectFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      // obj_file = NULL; // skip linking
      goto end;
    }
    RTIMER_LOG("llvm codegen MC object %s", obj_file);
  }

  // emit machine code (assembly)
  if (asm_file) {
    RTIMER_START();
    if (!llvm_emit_mc(mod, targetm, LLVMAssemblyFile, asm_file, &errmsg)) {
      errlog("llvm_emit_mc (LLVMAssemblyFile): %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen MC assembly %s", asm_file);
  }

  // emit LLVM bitcode
  if (bc_file) {
    RTIMER_START();
    if (!llvm_emit_bc(mod, bc_file, &errmsg)) {
      errlog("llvm_emit_bc: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen LLVM bitcode %s", bc_file);
  }

  // emit LLVM IR
  if (ir_file) {
    RTIMER_START();
    if (!llvm_emit_ir(mod, ir_file, &errmsg)) {
      errlog("llvm_emit_ir: %s", errmsg);
      LLVMDisposeMessage(errmsg);
      goto end;
    }
    RTIMER_LOG("llvm codegen LLVM IR text %s", ir_file);
  }

  // link executable
  if (exe_file && obj_file) {
    RTIMER_START();
    const char* inputv[] = { obj_file };
    CoLLDOptions lldopt = {
      .targetTriple = triple,
      .opt = build->opt,
      .outfile = exe_file,
      .infilec = countof(inputv),
      .infilev = inputv,
    };
    if (!lld_link(&lldopt, &errmsg)) {
      errlog("lld_link: %s", errmsg);
      goto end;
    }
    RTIMER_LOG("lld link executable %s", exe_file);

    // print warnings
    if (strlen(errmsg) > 0)
      fwrite(errmsg, strlen(errmsg), 1, stderr);
    LLVMDisposeMessage(errmsg);
  }


  // if we get here, without "goto end", all succeeded
  ok = true;

end:
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);
  return ok;
}

#if 0
__attribute__((constructor,used)) static void llvm_init() {
  Pkg pkg = {
    .dir  = ".",
    .id   = "foo/bar",
    .name = "bar",
  };
  Build build = {
    .pkg = &pkg,
    .opt = CoOptAggressive,
  };
  if (!llvm_build_and_emit(&build, /*target=host*/NULL)) {
    //
  }
  // exit(0);
}
#endif
