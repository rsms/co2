#include "../coimpl.h"
#include "parse.h"

static const char* const _DiagLevelName[DiagMAX + 1] = {
  "error",
  "warn",
  "note",
};

const char* DiagLevelName(DiagLevel l) {
  return _DiagLevelName[MAX(0, MIN(l, DiagMAX))];
}

Str diag_fmt(const Diagnostic* d, Str s) {
  assert(d->level <= DiagMAX);
  return pos_fmt(&d->build->posmap, d->pos, s,
    "%s: %s", DiagLevelName(d->level), d->message);
}

void diag_free(Diagnostic* d) {
  assert(d->build != NULL);
  mem_free(d->build->mem, (void*)d->message, strlen(d->message) + 1);
  mem_free(d->build->mem, d, sizeof(Diagnostic));
}
