#pragma once
#include "../util/symmap.h"
#include "../util/array.h"

ASSUME_NONNULL_BEGIN

// NodeClassFlags classifies AST Node kinds
typedef enum {
  NodeClassInvalid = 0,

  // category
  NodeClassConst   = 1 << 0, // literals like 123, true, nil.
  NodeClassExpr    = 1 << 1, // e.g. (+ x y)
  NodeClassType    = 1 << 2, // e.g. i32

  // data attributes
  NodeClassArray   = 1 << 3, // uses Node.array, or Node.t.array if NodeClassType
} NodeClassFlags;

// DEF_NODE_KINDS defines primary node kinds which are either expressions or start of expressions
#define DEF_NODE_KINDS(_) \
  /* N<kind>     classification */ \
  _(None,        NodeClassInvalid) \
  _(Bad,         NodeClassInvalid) /* substitute "filler node" for invalid syntax */ \
  _(BoolLit,     NodeClassConst) /* boolean literal */ \
  _(IntLit,      NodeClassConst) /* integer literal */ \
  _(FloatLit,    NodeClassConst) /* floating-point literal */ \
  _(Nil,         NodeClassConst) /* the nil atom */ \
  _(Comment,     NodeClassExpr) \
  _(Assign,      NodeClassExpr) \
  _(Arg,         NodeClassExpr) \
  _(Block,       NodeClassExpr|NodeClassArray) \
  _(Call,        NodeClassExpr) \
  _(Field,       NodeClassExpr) \
  _(Pkg,         NodeClassExpr|NodeClassArray) \
  _(File,        NodeClassExpr|NodeClassArray) \
  _(Fun,         NodeClassExpr) \
  _(Id,          NodeClassExpr) \
  _(If,          NodeClassExpr) \
  _(Let,         NodeClassExpr) \
  _(BinOp,       NodeClassExpr) \
  _(PrefixOp,    NodeClassExpr) \
  _(PostfixOp,   NodeClassExpr) \
  _(Return,      NodeClassExpr) \
  _(Tuple,       NodeClassExpr|NodeClassArray) \
  _(TypeCast,    NodeClassExpr) \
  _(ZeroInit,    NodeClassExpr) \
  _(BasicType,   NodeClassType) /* int, bool, ... */ \
  _(TupleType,   NodeClassType|NodeClassArray) /* (float,bool,int) */ \
  _(FunType,     NodeClassType) /* (int,int)->(float,bool) */ \
/*END DEF_NODE_KINDS*/

// NodeKind { NNone, NBad, NBoolLit, ... ,_NodeKindMax }
typedef enum {
  #define I_ENUM(name, _cls) N##name,
  DEF_NODE_KINDS(I_ENUM)
  #undef I_ENUM
  _NodeKindMax
} NodeKind;

// NodeKindName returns the name of node kind constant
const char* NodeKindName(NodeKind);

// DebugNodeClassStr returns a printable representation of NodeClassFlags. Not thread safe!
#ifdef DEBUG
  #define DebugNodeClassStr(fl) _DebugNodeClassStr((fl), __LINE__)
  const char* _DebugNodeClassStr(NodeClassFlags, u32 lineno);
#else
  #define DebugNodeClassStr(fl) ("NodeClassFlags")
#endif

// AST node types
typedef struct Node Node;
typedef Node Type;

// Scope represents a lexical namespace
typedef struct Scope Scope;
typedef struct Scope {
  u32          childcount; // number of scopes referencing this scope as parent
  const Scope* parent;
  SymMap       bindings;
} Scope;

Scope* ScopeNew(const Scope* nullable parent, Mem nullable mem);
void ScopeFree(Scope*, Mem nullable mem);
const Node* ScopeAssoc(Scope*, Sym, const Node* value); // Returns replaced value or NULL
const Node* ScopeLookup(const Scope*, Sym);
const Scope* GetGlobalScope();


// // NodeList is a linked list of nodes
// typedef struct NodeListLink NodeListLink;
// typedef struct NodeListLink {
//   Node*         node;
//   NodeListLink* next;
// } NodeListLink;
// typedef struct {
//   NodeListLink* head;
//   NodeListLink* tail;
//   u32           len;   // number of items
// } NodeList;

typedef Array NodeArray;

// NVal contains the value of basic types of literal nodes
typedef struct NVal {
  CType ct; // CType_bool | CType_int | CType_rune | CType_nil | CType_float | CType_str
  union {
    u64    i;  // BoolLit, IntLit
    double f;  // FloatLit
    Str    s;  // StrLit
  };
} NVal;

// TODO: add function for computing the end Pos for an AST node. E.g. last item of a tuple.

typedef struct Node {
  NodeKind       kind; // kind of node (e.g. NId)
  Pos            pos;  // source origin & position
  Node* nullable type; // value type. NULL if unknown.
  union {
    void* _never; // for initializers
    NVal val; // BoolLit, IntLit, FloatLit, StrLit
    /* str */ struct { // Comment
      const u8* ptr;
      size_t    len;
    } str;
    /* ref */ struct { // Id
      Sym   name;
      Node* target;
    } ref;
    /* op */ struct { // Op, PrefixOp, Return, Assign
      Node*          left;
      Node* nullable right;  // NULL for PrefixOp. NULL for Op when its a postfix op.
      Tok            op;
    } op;
    /* array */ struct { // Tuple, Block, File, Pkg
      Scope* nullable scope;        // non-NULL if kind==Block|File
      NodeArray       a;            // array of nodes
      Node*           a_storage[4]; // in-struct storage for the first few entries of a
    } array;
    /* fun */ struct { // Fun
      Scope*          scope;  // parameter scope
      Node*  nullable params; // input params (NTuple)
      Node*  nullable result; // output results (NTuple | NExpr)
      Sym    nullable name;   // NULL for lambda
      Node*  nullable body;   // NULL for fun-declaration
    } fun;
    /* call */ struct { // Call, TypeCast
      Node* receiver;      // either an NFun or a type (e.g. NBasicType)
      Node* nullable args; // NULL if there are no args, else a NTuple
    } call;
    /* field */ struct { // Arg, Field, Let
      Sym            name;
      Node* nullable init;  // Field: initial value (may be NULL). Let: final value (never NULL).
      u32            index; // Arg: argument index.
    } field;
    /* cond */ struct { // If
      Node*          cond;
      Node*          thenb;
      Node* nullable elseb; // NULL or expr
    } cond;

    // Type
    /* t */ struct {
      Sym nullable id; // lazy; initially NULL. Computed from Node.
      union {
        /* array */ struct { // TupleType
          NodeArray a;            // array of nodes
          Node*     a_storage[4]; // in-struct storage for the first few entries of a
        } array;
        /* basic */ struct { // BasicType
          TypeCode typeCode;
          Sym      name;
        } basic;
        /* fun */ struct { // FunType
          Node* nullable params; // kind==NTupleType
          Node* nullable result; // tuple or single type
        } fun;
      };
    } t;

  }; // union
} Node;

// Node* NodeAlloc(NodeKind); // one-off allocation using calloc()
// inline static void NodeFree(Node* _) {}
Str NodeRepr(const Node* nullable n, Str s); // return printable text representation

// fmtast returns an s-expression representation of an AST.
// Note: The returned string is garbage collected.
ConstStr fmtast(const Node* nullable n);

// fmtnode returns a short representation of an AST node, suitable for use in error messages.
// Note: The returned string is garbage collected.
ConstStr fmtnode(const Node* nullable n);

// str_append_astnode appends a short representation of an AST node to s.
// It produces the same result as fmtnode.
Str str_append_astnode(Str s, const Node* nullable n);

// NodeKindClass returns NodeClassFlags for kind. It's a fast inline table lookup.
static NodeClassFlags NodeKindClass(NodeKind kind);

// NodeKindIs{Type|Const|Expr} returns true if kind is of class Type, Const or Expr.
inline static bool NodeKindIsType(NodeKind kind) { return NodeKindClass(kind) & NodeClassType; }
inline static bool NodeKindIsConst(NodeKind kind) { return NodeKindClass(kind) & NodeClassConst;}
inline static bool NodeKindIsExpr(NodeKind kind) { return NodeKindClass(kind) & NodeClassExpr; }

// NodeIs{Type|Const|Expr} calls NodeKindIs{Type|Const|Expr}(n->kind)
inline static bool NodeIsType(const Node* n) { return NodeKindIsType(n->kind); }
inline static bool NodeIsConst(const Node* n) { return NodeKindIsConst(n->kind); }
inline static bool NodeIsExpr(const Node* n) { return NodeKindIsExpr(n->kind); }

// Retrieve the effective "printable" type of a node.
// For nodes which are lazily typed, like IntLit, this returns the default type of the constant.
const Node* NodeEffectiveType(const Node* n);

// NodeIdealCType returns a type for an arbitrary "ideal" (untyped constant) expression like "3".
CType NodeIdealCType(const Node* n);

// IdealType returns the constant type node for a ctype
Node* nullable IdealType(CType ct);

// NodeIsUntyped returns true for untyped constants, like for example "x = 123"
inline static bool NodeIsUntyped(const Node* n) {
  return n->type == Type_ideal;
}

// ast_opt_ifcond attempts to optimize an NIf node with constant expression conditions
Node* ast_opt_ifcond(Node* n);

// Format an NVal
Str NValFmt(Str s, const NVal* v);

// ArrayNodeLast returns the last element of array node n or NULL if empty
Node* nullable ArrayNodeLast(Node* n);

// NodeEndPos returns the Pos representing the logical inclusive end of the node.
// For example, for a tuple that is the pos of the last element.
// Returns NoPos if the end of the node is the same as n->pos.
Pos NodeEndPos(Node* n);

static void NodeArrayAppend(Mem nullable mem, Array* a, Node* n);
static void NodeArrayClear(Array* a);


extern const Node* NodeBad;  // kind==NBad

// NewNode allocates a node in mem
Node* NewNode(Mem nullable mem, NodeKind kind);

// NodeCopy creates a shallow copy of n in mem
static Node* NodeCopy(Mem nullable mem, const Node* n);

// -----------------------------------------------------------------------------------------------
// implementations

extern const NodeClassFlags _NodeClassTable[_NodeKindMax];
ALWAYS_INLINE static NodeClassFlags NodeKindClass(NodeKind kind) {
  return _NodeClassTable[kind];
}

inline static Node* NodeCopy(Mem nullable mem, const Node* n) {
  assert((NodeKindClass(n->kind) & NodeClassArray) == 0); // no support for copying these yet
  Node* n2 = (Node*)memalloc(mem, sizeof(Node));
  memcpy(n2, n, sizeof(Node));
  return n2;
}

ALWAYS_INLINE static void NodeArrayAppend(Mem nullable mem, Array* a, Node* n) {
  ArrayPush(a, n, mem);
}

ALWAYS_INLINE static void NodeArrayClear(Array* a) {
  ArrayClear(a);
}

ASSUME_NONNULL_END
