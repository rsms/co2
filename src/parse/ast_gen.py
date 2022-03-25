#!/usr/bin/env python3
#
# This script parses the struct heirarchy in ast.h and writes ast_gen.{h,c}
#
import re, sys, os, os.path, pprint
from collections import OrderedDict

def prettyrepr(obj):
  return pprint.pformat(obj, indent=2, sort_dicts=False)

def err(msg):
  print(msg)
  sys.exit(1)

# "struct name { struct? parent;"
node_struct_re = re.compile(r'\n\s*struct\s+([^\s\n]+)\s*\{[\n\s]*(?:struct\s+|)([^\s]+)\s*;')

# parse CLI
if len(sys.argv) < 4:
  err("usage: %s <inhfile> <outhfile> <outcfile>" % sys.argv[0])
inhfile = sys.argv[1]
outhfile = sys.argv[2]
outcfile = sys.argv[3]

# load files
inhsource = ''
outhsource = ''
outcsource = ''
with open(inhfile, "r") as f:
  inhsource = f.read()
with open(outhfile, "r") as f:
  outhsource = f.read()
with open(outcfile, "r") as f:
  outcsource = f.read()

# find all node struct definitions
typemap = OrderedDict()
for m in node_struct_re.finditer(inhsource):
  name = m.group(1)
  parent_name = m.group(2)

  m = typemap.setdefault(name, OrderedDict())
  parent_m = typemap.setdefault(parent_name, OrderedDict())
  parent_m[name] = m

# print("typemap:", prettyrepr(typemap))
Node = typemap["Node"]
# print(prettyrepr(Node))


def strip_node_suffix(name):
  if name.endswith('Node'):
    name = name[:len(name) - len('Node')]
  return name


def collect_leaf_names(leafnames_out, subtypes):
  for name, subtypes2 in subtypes.items():
    if len(subtypes2) == 0:
      leafnames_out.append(name)
    else:
      collect_leaf_names(leafnames_out, subtypes2)
leafnames = []  # matches enum order
collect_leaf_names(leafnames, Node)


def find_maxdepth(depth, subtypes):
  d = depth
  for subtypes2 in subtypes.values():
    d = find_maxdepth(depth + 1, subtypes2)
  return d
maxdepth = find_maxdepth(0, Node)


# find longest enum value name
def find_maxnamelen(a, b, name, subtypes):
  shortname = strip_node_suffix(name)
  if len(subtypes) > 0:
    name += '_END'
    shortname += '_END'
  a = max(a, len(name))
  b = max(b, len(shortname))
  for name, subtypes2 in subtypes.items():
    a2, b2 = find_maxnamelen(a, b, name, subtypes2)
    a = max(a, a2)
    b = max(b, b2)
  return a, b
maxnamelen, maxshortnamelen = find_maxnamelen(len('Node'), 0, 'Node', Node)


IND = '  '
def gen_NodeKind_enum(out, depth, i, subtypes):
  ind = IND * depth
  namelen = (maxdepth * len(IND) + maxshortnamelen +1) - len(ind) # +1 for 'N' prefix
  for name, subtypes2 in subtypes.items():
    shortname = strip_node_suffix(name)
    is_leaf = len(subtypes2) == 0
    if is_leaf:
      out.append(ind+'%-*s = %2d, // struct %s' % (namelen, 'N'+shortname, i, name))
      if leafnames[i] != name:
        err("unexpected name %r (leafnames out of order? found %r at leafnames[%d])" % (
          name, leafnames[i], i))
      i += 1
    else:
      out.append(ind+'%-*s = %2d,' % (namelen, 'N'+shortname+'_BEG', i))
    i = gen_NodeKind_enum(out, depth + 1, i, subtypes2)
    if not is_leaf:
      out.append(ind+'%-*s = %2d,' % (namelen, 'N'+shortname+'_END', i - 1))
  if depth == 1:
    out.append(ind+'%-*s = %2d,' % (namelen, 'NodeKind_MAX', i - 1))
  return i


def output_compact(output, input, maxcol, indent, lineend=''):
  buf = ""
  for w in input:
    if len(buf) + len(w) > maxcol and len(buf) > 0:
      output.append(buf[:len(buf)-1] + lineend)
      buf = indent
    buf += w + ' '
  if len(buf) > 0:
    output.append(buf[:len(buf)-1])


def output_compact_macro(output, input, indent=1):
  line1idx = len(output)
  output_compact(output, input, 88, '  ' * indent, ' \\')
  output[line1idx] = ('  ' * (indent - 1)) + output[line1idx].strip()


def structname(nodename, subtypes):
  if len(subtypes) == 0 or nodename in ('Node', 'Stmt', 'Expr', 'Type'):
    return nodename
  return 'struct '+nodename


# ---------------------------------------------------------------------------------------

def generated_by_comment(outfile):
  return '// generated by %s -- do not edit' % (
    os.path.relpath(os.path.abspath(__file__), os.path.dirname(outfile)))

outh = [generated_by_comment(outhfile)]
outc = [generated_by_comment(outcfile)]

outh.append('ASSUME_NONNULL_BEGIN')
outh.append('')

outc.append('#include "../coimpl.h"')
outc.append('#include "ast.h"')
outc.append('')

# enum NodeKind
outh.append('enum NodeKind {')
nodekind_max = gen_NodeKind_enum(outh, 1, 0, Node) - 1
outh.append('} END_ENUM(NodeKind)')
outh.append('')

# NodeKindName
outh.append('// NodeKindName returns a printable name. E.g. %s => "%s"' % (
  'N'+strip_node_suffix(leafnames[0]), strip_node_suffix(leafnames[0]) ))
outh.append('const char* NodeKindName(NodeKind);')
outh.append('')
outc.append('const char* NodeKindName(NodeKind k) {')
outc.append('  // kNodeNameTable[NodeKind] => const char* name')
outc.append('  static const char* const kNodeNameTable[NodeKind_MAX+2] = {')
outtmp = ['"%s",' % strip_node_suffix(name) for name in leafnames]
outtmp.append('"?"')
output_compact(outc, outtmp, 80, '    ')
outc.append('  };')
outc.append('  return kNodeNameTable[MIN(NodeKind_MAX+2,k)];')
outc.append('}')
outc.append('')

# node typedefs
outh += ['typedef struct %s %s;' % (name, name) for name in leafnames]
outh.append('')

# NodeKindIs*
outh.append('// bool NodeKindIs<kind>(NodeKind)')
for name, subtypes in typemap.items():
  if len(subtypes) == 0:
    continue # skip leafs
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  outh.append('#define NodeKindIs%s(k) (N%s_BEG <= (k) && (k) <= N%s_END)' % (
    shortname, shortname, shortname))
outh.append('')

# is_*
outh.append('// bool is_<kind>(const Node*)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append('#define is_%s(n) ((n)->kind==N%s)' % (name, shortname))
  else:
    outh.append('#define is_%s(n) NodeKindIs%s((n)->kind)' % (name, shortname))
outh.append('')

# assert_is_*
outh.append('// void assert_is_<kind>(const Node*)')
outh.append('#ifdef DEBUG')
outh.append('#define _assert_is1(NAME,n) ({ \\')
outh.append('  NodeKind nk__ = assertnotnull(n)->kind; \\')
outh.append('  assertf(NodeKindIs##NAME(nk__), "expected N%s; got N%s #%d", \\')
outh.append('          #NAME, NodeKindName(nk__), nk__); \\')
outh.append('})')
outh.append('#else')
outh.append('#define _assert_is1(NAME,n) ((void)0)')
outh.append('#endif')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append(
      '#define assert_is_%s(n) asserteq(assertnotnull(n)->kind,N%s)' % (
      name, shortname))
  else:
    outh.append(
      '#define assert_is_%s(n) _assert_is1(%s,(n))' % (
      name, shortname))
outh.append('')

# as_*
# mode = "assert" | "generic" | "none"
def gen_as_TYPE(out, mode, name, subtypes):
  stname = structname(name, subtypes)
  if mode == "none":
    out.append('  #define as_%s(n) ((%s*)(n))' % (name, stname))
    out.append('  #define as_const_%s(n) ((const %s*)(n))' % (name, stname))
  elif mode == "assert":
    out.append('  #define as_%s(n) ({ assert_is_%s(n); (%s*)(n); })' % (name, name, stname))
    out.append('  #define as_const_%s(n) ({ assert_is_%s(n); (const %s*)(n); })' % (
      name, name, stname))
  else: # mode == "generic"
    def visit(out, qual, name, subtypes):
      for name2, subtypes2 in subtypes.items():
        visit(out, qual, name2, subtypes2)
      if mode == "generic":
        #out.append('const %s*:(const %s*)(n),' % (structname(name, subtypes), stname))
        out.append('%s%s*:(%s%s*)(n),' % (qual, structname(name, subtypes), qual, stname))

    tmp = []
    tmp.append('  #define as_%s(n) _Generic((n),' % (name))
    visit(tmp, "", name, subtypes)
    if strip_node_suffix(name) != '':
      #tmp.append('const Node*: ({ assert_is_%s(n); (const %s*)(n); }),' % (name, stname))
      tmp.append('Node*: ({ assert_is_%s(n); (%s*)(n); }))' % (name, stname))
      # tmp.append('default: ({ assert_is_%s(n); (%s*)(n); }))' % (name, stname))
    tmp[-1] = tmp[-1][:-1] + ')' # replace last ',' with ')'
    output_compact_macro(out, tmp, indent=2)

    tmp = []
    tmp.append('  #define as_const_%s(n) _Generic((n),' % (name))
    visit(tmp, "const ", name, subtypes)
    if strip_node_suffix(name) != '':
      #tmp.append('const Node*: ({ assert_is_%s(n); (const %s*)(n); }),' % (name, stname))
      tmp.append('const Node*: ({ assert_is_%s(n); (const %s*)(n); }))' % (name, stname))
      # tmp.append('default: ({ assert_is_%s(n); (%s*)(n); }))' % (name, stname))
    tmp[-1] = tmp[-1][:-1] + ')' # replace last ',' with ')'
    output_compact_macro(out, tmp, indent=2)

    out.append('')

def gen_as_TYPE_supers(out, mode, subtypes):
  for name, subtypes2 in subtypes.items():
    if subtypes2:
      gen_as_TYPE(out, mode, name, subtypes2)
      gen_as_TYPE_supers(out, mode, subtypes2)

def gen_as_TYPE_leafs(out, mode):
  for name, subtypes in typemap.items():
    shortname = strip_node_suffix(name)
    if shortname == '' or len(subtypes) > 0: continue
    if mode == "none":
      out.append('  #define as_%s(n) ((%s*)(n))' % (name, name))
      out.append('  #define as_const_%s(n) ((const %s*)(n))' % (name, name))
    else: # mode == "assert" or mode == "generic"
      out.append('  #define as_%s(n) ({ assert_is_%s(n); (%s*)(n); })' % (name, name, name))
      out.append('  #define as_const_%s(n) ({ assert_is_%s(n); (const %s*)(n); })' % (
        name, name, name))

# as_Node
outh.append('// T* as_T(Node* n)')
outh.append('// const T* as_const_T(const Node* n)')
outh.append('//')
outh.append('// Large _Generic with both const and non-const cases ("const T*" & "T*")')
outh.append('// are really slow to compile, so we break up the "as_" macros into two forms.')
outh.append('#if defined(DEBUG)')
gen_as_TYPE(outh, "none", 'Node', Node)
# gen_as_TYPE_leafs(outh, "assert")
# gen_as_TYPE_supers(outh, "assert", Node)
gen_as_TYPE_leafs(outh, "generic")
gen_as_TYPE_supers(outh, "generic", Node)
outh.append('#else // !defined(DEBUG)')
gen_as_TYPE(outh, "none", 'Node', Node)
gen_as_TYPE_leafs(outh, "none")
gen_as_TYPE_supers(outh, "none", Node)
outh.append('#endif // DEBUG')
outh.append('')


# maybe_*
outh.append('// <type>* nullable maybe_<type>(Node* n)')
outh.append('// const <type>* nullable maybe_<type>(const Node* n)')
for name, subtypes in typemap.items():
  shortname = strip_node_suffix(name)
  if shortname == '':
    continue # skip root type
  if len(subtypes) == 0:
    outh.append('#define maybe_%s(n) (is_%s(n)?(%s*)(n):NULL)' % (name, name, name))
  else:
    outh.append('#define maybe_%s(n) (is_%s(n)?as_%s(n):NULL)' % (name, name, name))
outh.append('')


# TypeOfNode
visit_typeof_seen = set()
def visit_typeof(out, name, subtypes, action, constaction):
  for name2, subtypes2 in subtypes.items():
    visit_typeof(out, name2, subtypes2, action, constaction)
  out.append('const %s*:%s,' % (structname(name, subtypes), constaction))
  out.append('%s*:%s,' % (structname(name, subtypes), action))
  visit_typeof_seen.add(name)

outh.append('// Type* nullable TypeOfNode(Node* n)')
outh.append('// Type* TypeOfNode(Type* n)')
tmp = []
tmp.append('#define TypeOfNode(n) _Generic((n),')
visit_typeof(tmp, 'Type', typemap['Type'], 'kType_type', '(const Type*)kType_type')
visit_typeof(tmp, 'Expr', typemap['Expr'],
  '((Expr*)(n))->type', '(const Type*)((Expr*)(n))->type')

# remaining node kinds have no type
for name, subtypes in typemap.items():
  if name != 'Node' and name not in visit_typeof_seen:
    tmp.append('%s*:NULL,' % structname(name, subtypes))
# inspect "Node*" at runtime
tmp += [
  'const Node*: ( is_Type(n) ? (const Type*)kType_type :',
  ' is_Expr(n) ? (const Type*)((Expr*)(n))->type : NULL ),',
  'Node*:( is_Type(n) ? kType_type : is_Expr(n) ? ((Expr*)(n))->type : NULL))',
]
output_compact_macro(outh, tmp)
outh.append('')


# union NodeUnion
outh.append('union NodeUnion {')
outtmp = ['%s _%d;' % (leafnames[i], i) for i in range(0, len(leafnames))]
output_compact(outh, outtmp, 80, '  ')
outh.append('};')
outh.append('')

# ASTVisitorFuns
ftable_size = len(leafnames) + 2
outh.append("""
typedef struct ASTVisitor     ASTVisitor;
typedef struct ASTVisitorFuns ASTVisitorFuns;
typedef int(*ASTVisitorFun)(ASTVisitor*, const Node*);
struct ASTVisitor {
  ASTVisitorFun ftable[%d];
};
void ASTVisitorInit(ASTVisitor*, const ASTVisitorFuns*);
""".strip() % ftable_size)

outh.append('// error ASTVisit(ASTVisitor* v, const NODE_TYPE* n)')
tmp = []
tmp.append('#define ASTVisit(v, n) _Generic((n),')
for name in leafnames:
  shortname = strip_node_suffix(name)
  tmp.append('const %s*: (v)->ftable[N%s]((v),(const Node*)(n)),' % (name,shortname))
  tmp.append('%s*: (v)->ftable[N%s]((v),(const Node*)(n)),' % (name,shortname))
for name, subtypes in typemap.items():
  if len(subtypes) > 0:
    stname = structname(name, subtypes)
    tmp.append('const %s*: (v)->ftable[(n)->kind]((v),(const Node*)(n)),' % stname)
    tmp.append('%s*: (v)->ftable[(n)->kind]((v),(const Node*)(n)),' % stname)
# tmp.append('default: v->ftable[MIN(%d,(n)->kind)]((v),(const Node*)(n)),' % (
#   ftable_size - 1))
tmp[-1] = tmp[-1][:-1] + ')' # replace last ',' with ')'
output_compact_macro(outh, tmp)
outh.append('')

outh.append('struct ASTVisitorFuns {')
for name in leafnames:
  shortname = strip_node_suffix(name)
  outh.append('  error(*nullable %s)(ASTVisitor*, const %s*);' % (shortname, name))

outh.append('')
outh.append("  // class-level visitors called for nodes without specific visitors")
for name, subtypes in typemap.items():
  if len(subtypes) == 0: continue
  shortname = strip_node_suffix(name)
  if shortname == '': continue
  stname = structname(name, subtypes)
  outh.append('  error(*nullable %s)(ASTVisitor*, const %s*);' % (shortname, stname))
outh.append('')
outh.append("  // catch-all fallback visitor")
outh.append('  error(*nullable Node)(ASTVisitor*, const Node*);')
outh.append('};')
outh.append('')

outc.append('static error ASTVisitorNoop(ASTVisitor* v, const Node* n) { return 0; }')
outc.append('')
outc.append('void ASTVisitorInit(ASTVisitor* v, const ASTVisitorFuns* f) {')
outc.append('  ASTVisitorFun dft = (f->Node ? f->Node : &ASTVisitorNoop), dft1 = dft;')
outc.append('  // populate v->ftable')

def visit(out, name, subtypes, islast=False):
  shortname = strip_node_suffix(name)
  if shortname != '':
    if len(subtypes) > 0:
      out.append('  // begin %s' % shortname)
      out.append('  if (f->%s) { dft1 = dft; dft = ((ASTVisitorFun)f->%s); }' % (
        shortname, shortname))
    else:
      outc.append('  v->ftable[N%s] = f->%s ? ((ASTVisitorFun)f->%s) : dft;' % (
        shortname, shortname, shortname))

  i = 0
  lasti = len(subtypes) - 1
  for name2, subtypes2 in subtypes.items():
    visit(out, name2, subtypes2, islast=(shortname=='' and i == lasti))
    i += 1

  if len(subtypes) > 0 and shortname != '':
    if islast:
      out.append('  // end %s' % shortname)
    else:
      out.append('  dft = dft1; // end %s' % shortname)

visit(outc, 'Node', Node)
outc.append('}')
outc.append('')

outh.append('ASSUME_NONNULL_END')

# # debug
# print("——— ast.h ———")
# print("\n".join(outh))
# print("——— ast.c ———")
# print("\n".join(outc))
# sys.exit(0)

with open(outhfile, "w") as f:
  f.write("\n".join(outh).strip() + "\n")

with open(outcfile, "w") as f:
  f.write("\n".join(outc).strip() + "\n")
