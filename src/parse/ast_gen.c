// generated by ast_gen.py -- do not edit
#include "parse.h"

const char* NodeKindName(NodeKind k) {
  // kNodeNameTable[NodeKind] => const char* name
  static const char* const kNodeNameTable[NodeKind_MAX+2] = {
"Bad", "Field", "Pkg", "File", "Comment", "Nil", "BoolLit", "IntLit",
    "FloatLit", "StrLit", "Id", "BinOp", "PrefixOp", "PostfixOp", "Return",
    "Assign", "Tuple", "Array", "Block", "Fun", "Template", "Call", "TypeCast",
    "Const", "Var", "Param", "TemplateParam", "Ref", "NamedArg", "Selector",
    "Index", "Slice", "If", "TypeExpr", "TypeType", "IdType", "AliasType",
    "RefType", "BasicType", "ArrayType", "TupleType", "StructType", "FunType",
    "TemplateType", "TemplateParamType", "?"
  };
  return kNodeNameTable[MIN(NodeKind_MAX+1,k)];
}
