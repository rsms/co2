#include "llvmimpl.h"
#include "../parse/parse.h"

// DEBUG_BUILD_EXPR: define to dlog trace build_expr
#define DEBUG_BUILD_EXPR


#define assert_llvm_type_iskind(llvmtype, expect_typekind) \
  asserteq(LLVMGetTypeKind(llvmtype), (expect_typekind))

#define assert_llvm_type_isptrkind(llvmtype, expect_typekind) do { \
  asserteq(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind); \
  asserteq(LLVMGetTypeKind(LLVMGetElementType(llvmtype)), (expect_typekind)); \
} while(0)

#define assert_llvm_type_isptr(llvmtype) \
  asserteq(LLVMGetTypeKind(llvmtype), LLVMPointerTypeKind)

// we check for so many null values in this file that a shorthand increases legibility
#define notnull assertnotnull_debug


// make the code more readable by using short name aliases
typedef LLVMValueRef      Val;
typedef LLVMTypeRef       Typ;
typedef LLVMBasicBlockRef Block;


typedef enum {
  Immutable = 0, // must be 0
  Mutable   = 1, // must be 1
} Mutability;


// B is internal data used during IR construction
typedef struct B {
  BuildCtx*       build; // Co build (package, mem allocator, etc)
  LLVMContextRef  ctx;
  LLVMModuleRef   mod;
  LLVMBuilderRef  builder;

  // debug info
  bool prettyIR; // if true, include names in the IR (function params, variables, etc)
  //std::unique_ptr<DIBuilder>   DBuilder;
  //DebugInfo                    debug;

  // development debugging support
  #ifdef DEBUG_BUILD_EXPR
  int log_indent;
  char dname_buf[128];
  #endif

  // optimization
  LLVMPassManagerRef FPM; // function pass manager

  // target
  LLVMTargetMachineRef target;

  // build state
  bool       noload;        // for NVar
  Mutability mut;       // true if inside mutable data context
  u32        fnest;         // function nest depth
  Val        varalloc;      // memory preallocated for a var's init
  SymMap     internedTypes; // AST types, keyed by typeid
  PMap       defaultInits;  // constant initializers (Typ => Val)

  // memory generation check (specific to current function)
  Block mgen_failb;
  Val   mgen_alloca; // alloca for failed ref (type "REF" { i32*, i32 })

  // type constants
  Typ t_void;
  Typ t_bool;
  Typ t_i8;
  Typ t_i16;
  Typ t_i32;
  Typ t_i64;
  // Typ t_i128;
  Typ t_f32;
  Typ t_f64;
  // Typ t_f128;
  Typ t_int;

  Typ t_i8ptr;  // i8*
  Typ t_i32ptr; // i32*

  // ref struct types
  Typ t_ref; // "mut&T", "&T"

  // value constants
  Val v_i32_0; // i32 0
  Val v_int_0; // int 0

  // metadata values
  Val md_br_likely;
  Val md_br_unlikely;

  // metadata "kind" identifiers
  u32 md_kind_prof; // "prof"

} B;


// development debugging support
#ifdef DEBUG_BUILD_EXPR
static char kSpaces[256];
#endif


#define CHECKNOMEM(expr) \
  ( UNLIKELY(expr) ? (b_errf(b->build, (PosSpan){0}, "out of memory"), true) : false )


static error builder_init(B* b, BuildCtx* build, LLVMModuleRef mod) {
  #ifdef DEBUG_BUILD_EXPR
  memset(kSpaces, ' ', sizeof(kSpaces));
  #endif

  LLVMContextRef ctx = LLVMGetModuleContext(mod);

  *b = (B){
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
    .t_i8   = LLVMInt8TypeInContext(ctx),
    .t_i16  = LLVMInt16TypeInContext(ctx),
    .t_i32  = LLVMInt32TypeInContext(ctx),
    .t_i64  = LLVMInt64TypeInContext(ctx),
    // .t_i128 = LLVMInt128TypeInContext(ctx),
    .t_f32  = LLVMFloatTypeInContext(ctx),
    .t_f64  = LLVMDoubleTypeInContext(ctx),
    // .t_f128 = LLVMFP128TypeInContext(ctx),

    // metadata "kind" identifiers
    .md_kind_prof = LLVMGetMDKindIDInContext(ctx, "prof", 4),
  };

  // initialize common types
  b->t_int = (build->sint_type == TC_i32) ? b->t_i32 : b->t_i64;
  b->t_i8ptr = LLVMPointerType(b->t_i8, 0);
  b->t_i32ptr = LLVMPointerType(b->t_i32, 0);
  b->v_i32_0 = LLVMConstInt(b->t_i32, 0, /*signext*/false);
  b->v_int_0 = LLVMConstInt(b->t_int, 0, /*signext*/false);

  // initialize containers
  if (symmap_init(&b->internedTypes, build->mem, 16) == NULL) {
    LLVMDisposeBuilder(b->builder);
    return err_nomem;
  }
  if (pmap_init(&b->defaultInits, build->mem, 16, MAPLF_2) == NULL) {
    LLVMDisposeBuilder(b->builder);
    symmap_free(&b->internedTypes);
    return err_nomem;
  }

  // initialize function pass manager
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

  return 0;
}


static void builder_dispose(B* b) {
  symmap_free(&b->internedTypes);
  hmap_dispose(&b->defaultInits);
  if (b->FPM)
    LLVMDisposePassManager(b->FPM);
  LLVMDisposeBuilder(b->builder);
}


static bool val_is_ret(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMRet;
}

static bool val_is_call(LLVMValueRef v) {
  return LLVMGetValueKind(v) == LLVMInstructionValueKind &&
         LLVMGetInstructionOpcode(v) == LLVMCall;
}


inline static Block get_current_block(B* b) {
  return LLVMGetInsertBlock(b->builder);
}

inline static Val get_current_fun(B* b) {
  return LLVMGetBasicBlockParent(get_current_block(b));
}


//———————————————————————————————————————————————————————————————————————————————————————
// begin type functions


#if DEBUG
  __attribute__((used))
  static const char* fmttyp(Typ t) {
    if (!t)
      return "(null)";
    static char* p[5] = {NULL};
    static u32 index = 0;
    u32 i = index++;
    if (index == countof(p))
      index = 0;
    if (p[i])
      LLVMDisposeMessage(p[i]);
    p[i] = LLVMPrintTypeToString(t);
    return p[i];
  }
#endif


static Typ nullable get_interned_type(B* b, Type* tn) {
  assert_is_Type(tn);
  Sym tid = b_typeid(b->build, tn);
  return symmap_find(&b->internedTypes, tid);
}


static bool set_interned_type(B* b, Type* tn, Typ tr) {
  assert_is_Type(tn);
  Sym tid = b_typeid(b->build, tn);
  void** valp = symmap_assign(&b->internedTypes, tid);
  if CHECKNOMEM(valp == NULL)
    return false;
  *valp = tr;
  return true;
}


//———————————————————————————————————————————————————————————————————————————————————————
// begin type build functions


// get_type(B, AST type) => LLVM type
#define get_type(b, ast_type) \
  ({ Type* tn__=as_Type(ast_type); tn__ ? _get_type((b),tn__) : (b)->t_void; })
static Typ nullable _get_type(B* b, Type* np);


static Typ build_funtype(B* b, FunTypeNode* tn) {
  // first register a marker for the function type to avoid cyclic get_type,
  // i.e. in case result or parameters referst to the same type.
  set_interned_type(b, as_Type(tn), b->t_void);

  Typ rettype = get_type(b, tn->result);
  Typ paramsv[16];
  auto paramtypes = array_make(Array(Typ), paramsv, sizeof(paramsv));

  if (tn->params) {
    for (u32 i = 0; i < tn->params->a.len; i++) {
      Typ t = get_type(b, assertnotnull(tn->params->a.v[i]->type));
      assertf(t != b->t_void, "invalid type: %s", fmttyp(t));
      CHECKNOMEM(!array_push(&paramtypes, t));
    }
  }

  bool isVarArg = false;
  Typ ft = LLVMFunctionType(rettype, paramtypes.v, paramtypes.len, isVarArg);

  set_interned_type(b, as_Type(tn), ft);
  array_free(&paramtypes);
  return ft;
}


static Typ nullable get_basic_type(B* b, BasicTypeNode* tn) {
  switch (tn->typecode) {
    case TC_bool:               return b->t_bool;
    case TC_i8:  case TC_u8:    return b->t_i8;
    case TC_i16: case TC_u16:   return b->t_i16;
    case TC_i32: case TC_u32:   return b->t_i32;
    case TC_i64: case TC_u64:   return b->t_i64;
    case TC_f32:                return b->t_f32;
    case TC_f64:                return b->t_f64;
    case TC_int: case TC_uint:  return b->t_int;
    case TC_nil: case TC_ideal: return b->t_void;
  }
  assertf(0,"unexpected type code %u", tn->typecode);
  return b->t_void;
}


static Typ nullable _get_type(B* b, Type* np) {
  if (np->kind == NBasicType)
    return get_basic_type(b, (BasicTypeNode*)np);

  Typ t = get_interned_type(b, np);
  if (t)
    return t;

  switch ((enum NodeKind)np->kind) { case NBad: {
    NCASE(TypeType)   panic("TODO %s", nodename(n));
    NCASE(NamedType)  panic("TODO %s", nodename(n));
    NCASE(AliasType)  panic("TODO %s", nodename(n));
    NCASE(RefType)    panic("TODO %s", nodename(n));
    NCASE(ArrayType)  panic("TODO %s", nodename(n));
    NCASE(TupleType)  panic("TODO %s", nodename(n));
    NCASE(StructType) panic("TODO %s", nodename(n));
    NCASE(FunType)    return build_funtype(b, n);
    NDEFAULTCASE      break;
  }}
  assertf(0,"invalid node kind: n@%p->kind = %u", np, np->kind);
  return NULL;
}


// end type build functions
//———————————————————————————————————————————————————————————————————————————————————————
// begin value build functions


static Val build_store(B* b, Val dst, Val val) {
  // really just LLVMBuildStore with assertions enabled in DEBUG builds
  #if DEBUG
  Typ dst_type = LLVMTypeOf(dst);
  asserteq(LLVMGetTypeKind(dst_type), LLVMPointerTypeKind);
  if (LLVMTypeOf(val) != LLVMGetElementType(dst_type)) {
    panic("store destination type %s != source type %s",
      fmttyp(LLVMGetElementType(dst_type)), fmttyp(LLVMTypeOf(val)));
  }
  #endif
  return LLVMBuildStore(b->builder, val, dst);
}


static Val build_expr(B* b, Expr* n, const char* vname) {
  dlog("TODO");
  return b->v_int_0;
}


static Val build_funproto(B* b, FunNode* n, const char* name) {
  // get or build function type
  Typ ft = get_type(b, n->type);
  if UNLIKELY(!ft)
    return NULL;
  Val fn = LLVMAddFunction(b->mod, name, ft);

  // set argument names (for debugging)
  if (b->prettyIR) {
    for (u32 i = 0, len = n->params ? n->params->a.len : 0; i < len; i++) {
      ParamNode* param = as_ParamNode(n->params->a.v[i]);
      Val p = LLVMGetParam(fn, i);
      LLVMSetValueName2(p, param->name, symlen(param->name));
    }
  }

  return fn;
}


static Val build_fun(B* b, FunNode* n, const char* vname) {
  vname = n->name ? n->name : vname;

  // build function prototype
  Val fn = build_funproto(b, n, vname);
  n->irval = fn;

  if (!n->body) { // external
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    return fn;
  }

  if (strcmp(vname, "main") != 0) {
    // Note on LLVMSetVisibility: visibility is different.
    // See https://llvm.org/docs/LangRef.html#visibility-styles
    // LLVMPrivateLinkage is like "static" in C but omit from symbol table
    // LLVMSetLinkage(fn, LLVMPrivateLinkage);
    LLVMSetLinkage(fn, LLVMInternalLinkage); // like "static" in C
  }

  // prepare to build function body by saving any current builder position
  Block prevb = get_current_block(b);
  Block mgen_failb = b->mgen_failb;
  Val mgen_alloca = b->mgen_alloca;
  b->mgen_failb = NULL;
  b->mgen_alloca = NULL;
  b->fnest++;

  // create a new basic block to start insertion into
  Block entryb = LLVMAppendBasicBlockInContext(b->ctx, fn, ""/*"entry"*/);
  LLVMPositionBuilderAtEnd(b->builder, entryb);

  // create local storage for mutable parameters
  for (u32 i = 0, len = n->params ? n->params->a.len : 0; i < len; i++) {
    ParamNode* pn = as_ParamNode(n->params->a.v[i]);
    Val pv = LLVMGetParam(fn, i);
    Typ pt = LLVMTypeOf(pv);
    if (NodeIsConst(pn) /*&& !arg_type_needs_alloca(pt)*/) {
      // immutable pimitive value does not need a local alloca
      pn->irval = pv;
      continue;
    }
    // give the local a helpful name
    const char* name = pn->name;
    #if DEBUG
      char namebuf[128];
      snprintf(namebuf, sizeof(namebuf), "arg_%s", name);
      name = namebuf;
    #endif
    pn->irval = LLVMBuildAlloca(b->builder, pt, name);
    build_store(b, pn->irval, pv);
  }

  // build body
  Val bodyval = build_expr(b, n->body, "");

  // handle implicit return at end of body
  if (!bodyval || !val_is_ret(bodyval)) {
    if (!bodyval || as_FunTypeNode(n->type)->result == kType_nil) {
      LLVMBuildRetVoid(b->builder);
    } else {
      if (val_is_call(bodyval)) {
        // TODO: might need to add a condition for matching parameters & return type
        LLVMSetTailCall(bodyval, true);
      }
      LLVMBuildRet(b->builder, bodyval);
    }
  }

  // make sure failure blocks are at the end of the function,
  // since they are less likely to be visited.
  if (b->mgen_failb) {
    Block lastb = LLVMGetLastBasicBlock(fn);
    if (lastb != b->mgen_failb)
      LLVMMoveBasicBlockAfter(b->mgen_failb, lastb);
  }

  // restore any current builder position
  if (prevb) {
    LLVMPositionBuilderAtEnd(b->builder, prevb);
    b->mgen_failb = mgen_failb;
    b->mgen_alloca = mgen_alloca;
  }
  b->fnest--;

  return fn;
}


static Val build_global_var(B* b, VarNode* n) {
  dlog("TODO");
  return NULL;
}


static void build_file(B* b, FileNode* n) {
  dlog("build_file %s", n->name);
  LLVMSetSourceFileName(b->mod, n->name, (usize)strlen(n->name));

  // first build all globals ...
  for (u32 i = 0; i < n->a.len; i++) {
    Node* np = n->a.v[i];
    if (np->kind == NVar)
      build_global_var(b, as_VarNode(np));
  }

  // ... then functions
  for (u32 i = 0; i < n->a.len; i++) {
    Node* np = n->a.v[i];
    switch ((enum NodeKind)np->kind) { case NBad: {
      NCASE(Fun)
        assertnotnull(n->name); // top-level functions are named
        build_fun(b, n, n->name);
      NCASE(Var)
        // ignore
      NDEFAULTCASE
        panic("TODO: file-level %s", NodeKindName(np->kind));
    }}
  }
}


static void build_pkg(B* b, PkgNode* n) {
  NodeArray* na = &b->build->pkg.a;
  for (u32 i = 0; i < na->len; i++) {
    FileNode* file = as_FileNode(na->v[i]);
    build_file(b, file);
  }
}

// end build functions
//———————————————————————————————————————————————————————————————————————————————————————

error llvm_build_module(BuildCtx* build, LLVMModuleRef mod) {
  // initialize builder
  B b_; B* b = &b_;
  error err = builder_init(b, build, mod);
  if (err)
    return err;

  // build package
  build_pkg(b, &build->pkg);

  // verify IR
  #ifdef DEBUG
    char* errmsg;
    bool ok = LLVMVerifyModule(b->mod, LLVMPrintMessageAction, &errmsg) == 0;
    if (!ok) {
      //errlog("=========== LLVMVerifyModule ===========\n%s\n", errmsg);
      LLVMDisposeMessage(errmsg);
      dlog("\n=========== LLVMDumpModule ===========");
      LLVMDumpModule(b->mod);
      builder_dispose(b);
      return err_invalid;
    }
  #endif

  // finalize all function passes scheduled in the function pass
  if (b->FPM)
    LLVMFinalizeFunctionPassManager(b->FPM);

  // log LLVM IR
  #ifdef DEBUG
    dlog("LLVM IR module as built:");
    LLVMDumpModule(b->mod);
  #endif

  // cleanup
  builder_dispose(b);
  return err;
}
