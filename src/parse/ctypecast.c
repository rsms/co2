#include "../coimpl.h"
#include "ctypecast.h"

Expr* _ctypecast(
  BuildCtx* b, Expr* n, Type* t, CTypecastResult* nullable resp, CTypecastFlags fl)
{
  assertnotnull(t);
  CTypecastResult restmp;
  CTypecastResult* res = resp ? resp : &restmp;

  if (n->type && b_typeeq(b, n->type, t)) {
    *res = CTypecastUnchanged;
    return n;
  }

  // TODO
  *res = CTypecastErrCompat;
  return n;
}
