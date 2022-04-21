#include "parse.h"

// DEBUG_INCLUDE_POINTERS: define to include node memory addresses in output
//#define DEBUG_INCLUDE_POINTERS

// INDENT_DEPTH is the number of spaces used for indentation
#define INDENT_DEPTH 2

static ABuf* _fmtnode1(const Node* nullable n, ABuf* s);

char* _fmtnode(const Node* nullable n, char* buf, usize bufcap) {
  ABuf s = abuf_make(buf, bufcap);
  _fmtnode1(n, &s);
  abuf_terminate(&s);
  return buf;
}

static ABuf* _fmtnodearray(const NodeArray* na, ABuf* s) {
  for (u32 i = 0; i < na->len; i++) {
    if (i) abuf_c(s, ' ');
    _fmtnode1(na->v[i], s);
  }
  return s;
}

static ABuf* _fmtnode1(const Node* nullable n, ABuf* s) {
  // Note: Do not include type information.
  // Instead, in use sites, call fmtnode individually for n->type when needed.

  #define NODE(s,n)      _fmtnode1(as_Node(n), s)
  #define NODEARRAY(s,a) _fmtnodearray(as_NodeArray(a), s)
  #define CH(s,c)        abuf_c(s, c)
  #define STR(s,cstr)    abuf_cstr(s, cstr)
  #define SYM(s,sym)     abuf_append(s, sym, symlen(sym))
  #define SYMX(s,sym)    ((sym) ? abuf_append(s, (sym), symlen(sym)) : abuf_c(s, '_'))

  if (n == NULL)
    return STR(s,"<null>");

  switch ((enum NodeKind)n->kind) {

  case NBad: // nil
    return STR(s,"bad");

  case NPkg: // package "foo"
    return CH(STR(STR(s, "package \""), ((PkgNode*)n)->name ), '"');

  case NFile: // file "foo"
    return CH(STR(STR(s, "file \""), ((FileNode*)n)->name ), '"');

  case NField: // field foo T
    SYM(STR(s, "field"), ((FieldNode*)n)->name);
    if (((FieldNode*)n)->type)
      NODE(CH(s, ' '), ((FieldNode*)n)->type);
    return s;

  case NNil: // nil
    return STR(s, "nil");

  case NBoolLit: // true | false
    return STR(s, ((BoolLitNode*)n)->ival ? "true" : "false");

  case NIntLit: // 123
    return abuf_u64(s, ((IntLitNode*)n)->ival, 10);

  case NFloatLit: // 12.3
    return abuf_f64(s, ((FloatLitNode*)n)->fval, -1);

  case NStrLit: // "lolcat"
    return CH(abuf_repr(CH(s, '"'), ((StrLitNode*)n)->p, ((StrLitNode*)n)->len), '"');

  case NId: // foo
    return SYM(s, ((IdNode*)n)->name);

  case NBinOp: // foo + bar
    NODE(s, ((BinOpNode*)n)->left);
    STR(CH(s,' '), TokName(((BinOpNode*)n)->op));
    return NODE(CH(s,' '), ((BinOpNode*)n)->right);

  case NPostfixOp: // foo++
    NODE(s, ((PostfixOpNode*)n)->expr);
    return STR(s, TokName(((PostfixOpNode*)n)->op));

  case NPrefixOp: // -foo
    STR(s, TokName(((PrefixOpNode*)n)->op));
    return NODE(s, ((PrefixOpNode*)n)->expr);

  case NAssign: // foo=
    return CH(NODE(s, ((AssignNode*)n)->dst), '=');

  case NNamedArg: // name=value
    CH(SYM(s, ((NamedArgNode*)n)->name), '=');
    return NODE(s, ((NamedArgNode*)n)->value);

  case NReturn: // return foo
    return NODE(STR(s, "return "), ((ReturnNode*)n)->expr);

  case NBlock: // block
    if (NodeIsUnsafe(n)) STR(s, "unsafe ");
    return STR(s, "block");

  case NArray: // array [one two 3]
    return CH(NODEARRAY(STR(s, "array ["), &((ArrayNode*)n)->a), ']');

  case NTuple: // tuple (one two 3)
    return CH(NODEARRAY(STR(s, "tuple ("), &((TupleNode*)n)->a), ')');

  case NConst: // const x
    return SYM(STR(s, "const "), ((LocalNode*)n)->name);

  case NVar: // var x
    return SYM(STR(s, "var "), ((LocalNode*)n)->name);

  case NParam: // param x
    return SYM(STR(s, "param "), ((LocalNode*)n)->name);

  case NTemplateParam: // template_param T
    return SYM(STR(s, "tparam "), ((LocalNode*)n)->name);

  case NRef: // &x, mut&x
    return NODE(STR(s, NodeIsConst(n) ? "&" : "mut&"), ((RefNode*)n)->target);

  case NFun: // function foo
    if (NodeIsUnsafe(n)) STR(s, "unsafe ");
    return SYMX(STR(s, "function "), ((FunNode*)n)->name);

  case NTemplate: // template foo
    STR(s, "template");
    if (((TemplateNode*)n)->name)
      SYMX(CH(s, ' '), ((TemplateNode*)n)->name);
    return s;

  case NTemplateInstance: // template foo
    STR(s, "template-instance");
    if (((TemplateInstanceNode*)n)->tpl->name)
      SYMX(CH(s, ' '), ((TemplateInstanceNode*)n)->tpl->name);
    return s;

  case NTypeCast: // typecast<int16>
    return CH(NODE(STR(s, "typecast<"), ((TypeCastNode*)n)->expr), '>');

  case NCall: // call foo
    return NODE(STR(s, "call "), ((CallNode*)n)->receiver);

  case NIf: // if
    return STR(s, "if");

  case NSelector: // expr.name | expr.selector
    return SYM(CH(NODE(s, ((SelectorNode*)n)->operand), '.'), ((SelectorNode*)n)->member);

  case NIndex: // foo[index]
    CH(NODE(s, ((IndexNode*)n)->operand), '[');
    CH(NODE(s, ((IndexNode*)n)->indexexpr), ']');
    return s;

  case NSlice: { // [start?:end?]
    SliceNode* slice = (SliceNode*)n;
    NODE(s, slice->operand);
    CH(s, '[');
    if (slice->start)
      NODE(s, slice->start);
    CH(s, ':');
    if (slice->end)
      NODE(s, slice->end);
    return CH(s, ']');
  }

  case NTypeExpr: // type foo
    return NODE(STR(s, "type "), ((TypeExprNode*)n)->elem);



  case NBasicType: // int
    return SYM(s, ((BasicTypeNode*)n)->name);

  case NRefType: // &T, mut&T
    return NODE(STR(s, NodeIsConst(n) ? "&" : "mut&"), ((RefTypeNode*)n)->elem);

  case NTypeType: // type
    return STR(s, "type");

  case NIdType: // foo
    //return SYM(STR(s, "idtype "), ((IdTypeNode*)n)->name);
    return SYM(s, ((IdTypeNode*)n)->name);

  case NAliasType: // foo (aka bar)
    STR(SYM(s, ((AliasTypeNode*)n)->name), " (aka ");
    return CH(NODE(s, ((AliasTypeNode*)n)->elem), ')');

  case NFunType: { // (int int)->bool
    FunTypeNode* ft = (FunTypeNode*)n;
    STR(s, "fun(");
    for (u32 i = 0; i < ft->params->len; i++) {
      if (i) abuf_cstr(s, ", ");
      STR(s, ft->params->v[i]->name);
      abuf_c(s, ' ');
      NODE(s, ft->params->v[i]->type);
    }
    CH(s, ')');
    if (((FunTypeNode*)n)->result)
      NODE(abuf_c(s, ' '), ((FunTypeNode*)n)->result); // ok if NULL
    return s;
  }

  case NTemplateType: // type
    return STR(STR(s, TypeKindName(((TemplateTypeNode*)n)->prodkind)), " template ");

  case NTupleType: // (int bool Foo)
    return CH(NODEARRAY(CH(s, '('), &((TupleTypeNode*)n)->a), ')');

  case NArrayType: // [int], [int 4]
    NODE(CH(s, '['), ((ArrayTypeNode*)n)->elem);
    if (((ArrayTypeNode*)n)->size > 0)
      abuf_u64(CH(s,' '), ((ArrayTypeNode*)n)->size, 10);
    return CH(s, ']');

  case NStructType: { // "struct Name" or "struct {foo float; y bool}"
    StructTypeNode* st = (StructTypeNode*)n;
    STR(s, "struct ");
    if (st->name)
      return SYM(s, st->name);
    CH(s, '{');
    for (u32 i = 0; i < st->fields.len; i++) {
      auto field = st->fields.v[i];
      if (i)
        STR(s, "; ");
      SYM(s, field->name);
      if (field->type)
        NODE(CH(s,' '), field->type);
    }
    return CH(s, '}');
  }

  case NTemplateParamType:
    // return SYM(STR(s, "tparam "), ((TemplateParamTypeNode*)n)->param->name);
    return SYM(s, ((TemplateParamTypeNode*)n)->param->name);

  case NComment:
    assertf(0, "unexpected node %s", nodename(n));
    break;
  }

  return STR(s, "INVALID");

  #undef STR
  #undef SYM
  #undef SPACE
  #undef NODEARRAY
  #undef NODE
}



// ---------------------

typedef struct Repr Repr;

struct Repr {
  Str  dst;
  bool dstok;
  bool intypeof; // true while inside <...> (typeof)

  usize indent;
  usize lnstart; // relative to buf.len
  usize wrapcol;
  usize stylelen; // buf.len-stylelen = nbytes written to buf excluding ANSI codes
  PMap  seenmap;

  NodeFmtFlag flags;
  TStyles     styles;
  TStyleStack stylestack;
  u32         lparen_pos; // dst.len at the time after writing '('
};

#define STYLE_NODE   ((TStyle[]){ TS_BOLD })        // node name
#define STYLE_LIT    ((TStyle[]){ TS_LIGHTGREEN })
#define STYLE_NAME   ((TStyle[]){ TS_LIGHTBLUE })   // symbolic names like Id, IdType, etc.
#define STYLE_OP     ((TStyle[]){ TS_LIGHTORANGE })
#define STYLE_TYPE   ((TStyle[]){ TS_DIM })
#define STYLE_META   ((TStyle[]){ TS_LIGHTPURPLE, TS_DIM })
#define STYLE_ERR    ((TStyle[]){ TS_RED })
#define STYLE_NODEID ((TStyle[]){ TS_DIM })
#define STYLE_PAREN  ((TStyle[]){ TS_DIM })  // "(" and ")"

#define TSTYLE_PUSH(r, style_list) \
  tstyle_pushv(&(r)->stylestack, style_list, countof(style_list))


// -- repr output writers

static usize printable_len(Repr* r) {
  // nbytes written to buf excluding ANSI codes
  return r->dst.len - r->stylelen;
}

#define write_push_style(r, style_list) \
  _write_push_style((r), style_list, countof(style_list))

static void _write_push_style(Repr* r, const TStyle* stylev, u32 stylec) {
  if (TStylesIsNone(r->styles))
    return;
  const char* s = tstyle_pushv(&r->stylestack, stylev, stylec);
  usize len = strlen(s);
  r->stylelen += len;
  str_append(&r->dst, s, len);
}

static void write_pop_style(Repr* r) {
  if (TStylesIsNone(r->styles))
    return;
  const char* s = tstyle_pop(&r->stylestack);
  usize len = strlen(s);
  r->stylelen += len;
  str_append(&r->dst, s, len);
}

static void write_paren_start(Repr* r) {
  if (TStylesIsNone(r->styles)) {
    str_appendc(&r->dst, '(');
    return;
  }
  u32 len1 = r->dst.len;
  str_appendcstr(&r->dst, TSTYLE_PUSH(r, STYLE_PAREN));
  str_appendc(&r->dst, '(');
  str_appendcstr(&r->dst, tstyle_pop(&r->stylestack));
  r->stylelen += r->dst.len - len1 - 1;
  r->lparen_pos = r->dst.len;
}

static void write_paren_end(Repr* r) {
  if (TStylesIsNone(r->styles)) {
    str_appendc(&r->dst, ')');
    return;
  }
  u32 len1 = r->dst.len;
  str_appendcstr(&r->dst, TSTYLE_PUSH(r, STYLE_PAREN));
  str_appendc(&r->dst, ')');
  str_appendcstr(&r->dst, tstyle_pop(&r->stylestack));
  r->stylelen += r->dst.len - len1 - 1;
}

static void write_newline(Repr* r) {
  str_push(&r->dst, '\n');
  r->lnstart = printable_len(r);
  str_appendfill(&r->dst, ' ', r->indent);
}

static void write_push_indent(Repr* r) {
  if (r->intypeof) {
    if (r->dst.len != r->lparen_pos)
      str_push(&r->dst, ' ');
  } else {
    r->indent += INDENT_DEPTH;
    write_newline(r);
  }
}

static void write_pop_indent(Repr* r) {
  if (!r->intypeof) {
    assertf(r->indent > 1, "write_pop_indent without matching write_push_indent");
    r->indent -= 2;
  }
}

typedef struct Meta Meta;
struct Meta {
  Repr* r;
  usize startlen;
};
static Meta meta_begin(Repr* r) {
  return (Meta){ .r = r, .startlen = r->dst.len };
}
static void _meta_start(Meta* mp) {
  if (mp->startlen == mp->r->dst.len) {
    str_push(&mp->r->dst, ' ');
    write_push_style(mp->r, STYLE_META);
    str_push(&mp->r->dst, '[');
  } else {
    str_push(&mp->r->dst, ' ');
  }
}
#define meta_write_entry_start(m) _meta_start(&(m))
#define meta_write_entry(m,s)     ( _meta_start(&(m)), str_appendcstr(&(m).r->dst, (s)) )
#define meta_end(m) _meta_end(&(m))
static void _meta_end(Meta* m) {
  if (m->startlen < m->r->dst.len) {
    str_push(&m->r->dst, ']');
    write_pop_style(m->r);
  }
}

#define write_node(r,n) _write_node((r),as_const_Node(n))
static void _write_node(Repr* r, const Node* nullable n);

static void write_array(Repr* r, const NodeArray* a) {
  // if (a->len == 0)
  //   return;
  if (r->dst.len != r->lparen_pos)
    str_push(&r->dst, ' ');
  write_paren_start(r);
  for (u32 i = 0; i < a->len; i++)
    write_node(r, a->v[i]);
  write_paren_end(r);
}

static void write_str(Repr* r, const char* s) {
  str_push(&r->dst, ' ');
  str_appendcstr(&r->dst, s);
}

static void write_qstr(Repr* r, const char* s, usize len) {
  write_push_style(r, STYLE_LIT);
  str_appendcstr(&r->dst, " \"");
  str_appendrepr(&r->dst, s, len);
  str_push(&r->dst, '"');
  write_pop_style(r);
}

static void write_name(Repr* r, Sym s) {
  if (r->dst.len != r->lparen_pos)
    str_push(&r->dst, ' ');
  write_push_style(r, STYLE_NAME);
  str_append(&r->dst, s, symlen(s));
  write_pop_style(r);
}


static bool maybe_cyclic_node(const Node* n) {
  switch (n->kind) {
    case NVar:
    case NConst:
    case NParam:
    case NFun:
    case NTemplate:
      return true;
    default:
      return false;
  }
}

static bool reg_cyclic_node(Repr* r, const Node* n, u32* nodeid) {
  uintptr* vp = pmap_assign(&r->seenmap, n);
  if (!vp) { // failed to allocate memory
    *nodeid = U32_MAX;
    return true;
  }
  if (*vp) { // already seen
    *nodeid = (u32)*vp;
    return false;
  }
  // newfound
  *nodeid = r->seenmap.len;
  *vp = (uintptr)r->seenmap.len;
  return true;
}


static void write_node_attrs(Repr* r, const Node* np);
static void write_node_fields(Repr* r, const Node* n);
static void _write_node1(Repr* r, const Node* nullable n);

static void _write_node(Repr* r, const Node* nullable n) {
  Str* dst = &r->dst;

  // bool is_long_line = printable_len(r) - r->lnstart > r->wrapcol;
  bool indent = dst->len && dst->v[dst->len - 1] != '<';
  if (indent) write_push_indent(r);

  if (n == NULL || n == (Node*)kExpr_nil) {
    // "nil"
    write_push_style(r, STYLE_LIT);
    str_appendcstr(dst, "nil");
    write_pop_style(r);
  } else {
    // if (is_Type(n)) write_push_style(r, STYLE_TYPE);

    if (is_BasicTypeNode(n)) {
      // "name" for BasicType (e.g. "int")
      if (!r->intypeof)
        write_push_style(r, STYLE_NODE);
      Sym name = ((BasicTypeNode*)n)->name;
      str_append(dst, name, symlen(name));
      if (!r->intypeof)
        write_pop_style(r);
    } else {
      // "(Node field1 field2 ...fieldN)" for everything else
      write_paren_start(r);
      _write_node1(r, n);
      write_paren_end(r);
    }

    // if (is_Type(n)) write_pop_style(r);
  }

  if (indent) write_pop_indent(r);
}

static void _write_node1(Repr* r, const Node* n) {
  Str* dst = &r->dst;

  bool is_newfound = true;
  u32 nodeid = 0;
  if (maybe_cyclic_node(n))
    is_newfound = reg_cyclic_node(r, n, &nodeid);

  // "NodeName"
  if (!r->intypeof)
    write_push_style(r, STYLE_NODE);
  str_appendcstr(dst, NodeKindName(n->kind));
  if (!r->intypeof)
    write_pop_style(r);

  // mark nodes that may appear in many places
  if (nodeid) {
    write_push_style(r, STYLE_NODEID);
    str_appendcstr(&r->dst, "#");
    str_appendu32(&r->dst, nodeid, 16);
    write_pop_style(r);
  }

  // memory address (for debugging)
  if (0) {
    str_appendcstr(&r->dst, " \e[9");
    str_appendc(&r->dst, '1'+(int)((uintptr)n%6));
    str_appendcstr(&r->dst, "m0x");
    str_appendu64(&r->dst, (u64)(uintptr)n, 16);
    str_appendcstr(&r->dst, "\e[39m");
  }

  write_node_attrs(r, n);

  // stop now if we have already "seen" this node
  if (!is_newfound && (n->kind != NParam || r->intypeof))
    return;

  // "<type>" of expressions
  if (is_Expr(n)) {
    auto typ = ((Expr*)n)->type;
    if (!r->intypeof) {
      str_push(dst, ' ');
      write_push_style(r, STYLE_TYPE);
      if (typ == NULL) {
        write_push_style(r, STYLE_ERR);
        str_appendcstr(dst, "<?>");
        write_pop_style(r);
      } else {
        str_push(dst, '<');
        r->intypeof = true;
        write_node(r, typ);
        r->intypeof = false;
        str_push(dst, '>');
      }
      write_pop_style(r);
    } else if (n->kind == NParam) {
      if (typ == NULL) {
        write_push_style(r, STYLE_ERR);
        str_appendcstr(dst, " ?");
        write_pop_style(r);
      } else {
        write_node(r, typ);
      }
    }
  }

  // "[meta]"
  auto m = meta_begin(r);
  NodeFlags fl = n->flags;
  if (fl & NF_Unresolved)  meta_write_entry(m, "unres");
  if (fl & NF_Const)       meta_write_entry(m, "const");
  if (fl & NF_Base)        meta_write_entry(m, "base");
  if (fl & NF_RValue)      meta_write_entry(m, "rval");
  if (fl & NF_Unused)      meta_write_entry(m, "unused");
  if (fl & NF_Public)      meta_write_entry(m, "pub");
  if (fl & NF_Named)       meta_write_entry(m, "named");
  if (fl & NF_PartialType) meta_write_entry(m, "partialtype");
  if (fl & NF_CustomInit)  meta_write_entry(m, "custominit");
  if (fl & NF_Unsafe)      meta_write_entry(m, "unsafe");
  if (fl & NF_Shared)      meta_write_entry(m, "shared");
  if (fl & NF_HasShared)   meta_write_entry(m, "HASSHARED");
  meta_end(m);

  write_node_fields(r, n);
}

#define write_TODO(r) _write_TODO(r, __FILE__, __LINE__)
static void _write_TODO(Repr* r, const char* file, u32 line) {
  str_push(&r->dst, ' ');
  write_push_style(r, (TStyle[]){TS_RED});
  str_appendcstr(&r->dst, "[TODO ");
  str_appendcstr(&r->dst, file);
  str_push(&r->dst, ':');
  str_appendu32(&r->dst, line, 10);
  str_push(&r->dst, ']');
  write_pop_style(r);
}

// -- visitor functions

static void write_node_attrs(Repr* r, const Node* np) {
  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Pkg)  write_qstr(r, n->name, strlen(n->name));
  NCASE(File) write_qstr(r, n->name, strlen(n->name));

  // -- expressions --
  NCASE(Nil) UNREACHABLE; // handled by _write_node
  NCASE(Return)
  NCASE(Assign)
  NCASE(Tuple)
  NCASE(Array)
  NCASE(Block)
  NCASE(Call)
  NCASE(TypeCast)
  NCASE(TypeExpr)
  NCASE(Ref)
  NCASE(Id)       write_name(r, n->name);
  GNCASE(Local)   write_name(r, n->name);
  NCASE(NamedArg) write_name(r, n->name);
  NCASE(Fun)      write_name(r, n->name ? n->name : kSym__);
  NCASE(Template) write_name(r, n->name ? n->name : kSym__);
  NCASE(TemplateInstance)
    write_name(r, n->tpl->name ? n->tpl->name : kSym__);
  NCASE(BinOp)
    write_push_style(r, STYLE_OP);
    write_str(r, TokName(n->op));
    write_pop_style(r);
  GNCASE(UnaryOp)
    write_push_style(r, STYLE_OP);
    write_str(r, TokName(n->op));
    write_pop_style(r);
  NCASE(BoolLit)
    str_push(&r->dst, ' ');
    write_push_style(r, STYLE_LIT);
    str_appendcstr(&r->dst, n->ival ? "true" : "false");
    write_pop_style(r);
  NCASE(IntLit)
    str_push(&r->dst, ' ');
    write_push_style(r, STYLE_LIT);
    str_appendu64(&r->dst, n->ival, 10);
    write_pop_style(r);
  NCASE(FloatLit)
    str_push(&r->dst, ' ');
    write_push_style(r, STYLE_LIT);
    str_appendf64(&r->dst, n->fval, -1);
    write_pop_style(r);
  NCASE(StrLit)
    write_qstr(r, n->p, n->len);

  // -- types --
  NCASE(BasicType) UNREACHABLE; // handled by _write_node
  NCASE(AliasType) write_name(r, n->name);
  NCASE(IdType)    write_name(r, n->name);
  NCASE(TemplateType)
    write_str(r, TypeKindName(((TemplateTypeNode*)n)->prodkind));
  NCASE(TemplateParamType)
    write_name(r, assertnotnull(n->param)->name);
  NCASE(TypeType)
  NCASE(ArrayType)
  NCASE(TupleType)
  NCASE(FunType)
  NCASE(RefType)

  // -- not implemented --
  NCASE(Field)      write_TODO(r);
  NCASE(Selector)   write_TODO(r);
  NCASE(Index)      write_TODO(r);
  NCASE(Slice)      write_TODO(r);
  NCASE(If)         write_TODO(r);
  NCASE(Comment)    write_TODO(r);
  NCASE(StructType) write_TODO(r);
  }}
}

static void write_node_fields(Repr* r, const Node* np) {
  switch ((enum NodeKind)np->kind) { case NBad: {

  NCASE(Pkg)  write_array(r, as_NodeArray(&n->a));
  NCASE(File) write_array(r, as_NodeArray(&n->a));

  // -- expressions --
  NCASE(Id)       write_node(r, n->target);
  NCASE(BinOp)    write_node(r, n->left); write_node(r, n->right);
  GNCASE(UnaryOp) write_node(r, n->expr);
  NCASE(Return)   write_node(r, n->expr);
  NCASE(Assign)   write_node(r, n->dst); write_node(r, n->val);
  NCASE(Tuple)    write_array(r, as_NodeArray(&n->a));
  NCASE(Array)    write_array(r, as_NodeArray(&n->a));
  NCASE(NamedArg) write_node(r, n->value);
  NCASE(Ref)      write_node(r, n->target);
  NCASE(TypeExpr) write_node(r, n->elem);
  NCASE(Block)
    if (n->a.len > 0)
      write_array(r, as_NodeArray(&n->a));
  NCASE(Fun)
    write_array(r, as_NodeArray(&n->params));
    write_node(r, n->result);
    write_node(r, n->body);
  NCASE(Template)
    write_array(r, as_NodeArray(&n->params));
    write_node(r, n->body);
  NCASE(TemplateInstance)
    write_node(r, n->tpl);
    write_array(r, as_NodeArray(&n->args));
  GNCASE(Local)
    if (LocalInitField(n))
      write_node(r, LocalInitField(n));
  NCASE(Call)
    write_node(r, n->receiver);
    write_array(r, as_NodeArray(&n->args));
  NCASE(TypeCast)
    write_node(r, n->type);
    write_node(r, n->expr);

  // -- types --
  NCASE(RefType)   write_node(r, n->elem);
  NCASE(AliasType) write_node(r, n->elem);
  NCASE(IdType)    write_node(r, n->target);
  NCASE(ArrayType)
    write_node(r, n->elem);
    if (n->size) {
      str_push(&r->dst, ' ');
      write_push_style(r, STYLE_LIT);
      str_appendu64(&r->dst, n->size, 10);
      write_pop_style(r);
    } else if (n->sizeexpr) {
      write_node(r, n->sizeexpr);
    }
  NCASE(TupleType)
    write_array(r, as_NodeArray(&n->a));
  NCASE(FunType)
    // funtype does not have explicit parameter type,
    // instead it is computed from the function definitions parameters.
    str_push(&r->dst, ' ');
    write_paren_start(r);
    for (u32 i = 0; i < n->params->len; i++) {
      ParamNode* param = n->params->v[i];
      if (i) str_push(&r->dst, ' ');
      write_paren_start(r);
      write_name(r, param->name);
      write_node(r, unbox_id_type(param->type));
      write_paren_end(r);
    }
    write_paren_end(r);
    write_node(r, n->result);

  NDEFAULTCASE break;
  }}
}


bool _fmtast(const Node* nullable n, Str* dst, NodeFmtFlag fl) {
  Repr r = {
    .dst = *dst,
    .dstok = true,
    .wrapcol = 30,
    .flags = fl,
  };
  pmap_init(&r.seenmap, mem_ctx(), 64, MAPLF_1);

  r.styles = TStylesForStderr();
  r.stylestack.styles = r.styles;

  write_node(&r, n);
  *dst = r.dst;

  hmap_dispose(&r.seenmap);

  if UNLIKELY(!str_push(dst, 0)) {
    // was not able to allocate more memory
    dst->v[dst->len-1] = 0;
    return false;
  }
  return true;
}
