#include <rbase/rbase.h>
#include "../util/tstyle.h"
#include "source.h"

#include <sys/stat.h>
#include <sys/mman.h>

static void SourceInit(const Pkg* pkg, Source* src, const char* filename) {
  memset(src, 0, sizeof(Source));
  auto namelen = strlen(filename);
  if (namelen == 0)
    panic("empty filename");
  if (!strchr(filename, PATH_SEPARATOR)) { // foo.c -> pkgdir/foo.c
    src->filename = path_join(pkg->dir, filename);
  } else {
    src->filename = str_cpyn(filename, namelen+1);
  }
  src->pkg = pkg;
}

bool SourceOpen(Source* src, const Pkg* pkg, const char* filename) {
  SourceInit(pkg, src, filename);

  src->fd = open(src->filename, O_RDONLY);
  if (src->fd < 0)
    return false;

  struct stat st;
  if (fstat(src->fd, &st) != 0) {
    auto _errno = errno;
    close(src->fd);
    errno = _errno;
    return false;
  }
  src->len = (size_t)st.st_size;

  return true;
}

void SourceInitMem(Source* src, const Pkg* pkg, const char* filename, const char* text, size_t len) {
  SourceInit(pkg, src, filename);
  src->fd = -1;
  src->body = (const u8*)text;
  src->len = len;
}

bool SourceOpenBody(Source* src) {
  if (src->body == NULL) {
    src->body = mmap(0, src->len, PROT_READ, MAP_PRIVATE, src->fd, 0);
    if (!src->body)
      return false;
    src->ismmap = true;
  }
  return true;
}

bool SourceCloseBody(Source* src) {
  bool ok = true;
  if (src->body) {
    if (src->ismmap) {
      ok = munmap((void*)src->body, src->len) == 0;
      src->ismmap = false;
    }
    src->body = NULL;
  }
  return ok;
}

bool SourceClose(Source* src) {
  auto ok = SourceCloseBody(src);
  if (src->fd > -1) {
    ok = close(src->fd) != 0 && ok;
    src->fd = -1;
  }
  return ok;
}

void SourceDispose(Source* src) {
  str_free(src->filename);
  src->filename = NULL;
}

void SourceChecksum(Source* src) {
  SHA1Ctx sha1;
  sha1_init(&sha1);
  auto chunksize = mem_pagesize();
  auto remaining = src->len;
  auto inptr = src->body;
  while (remaining > 0) {
    auto z = MIN(chunksize, remaining);
    sha1_update(&sha1, inptr, z);
    inptr += z;
    remaining -= z;
  }
  sha1_final(src->sha1, &sha1);
}

// -----------------------------------------------------------------------------------------------
// Pkg

void PkgAddSource(Pkg* pkg, Source* src) {
  if (pkg->srclist)
    src->next = pkg->srclist;
  pkg->srclist = src;
}

bool PkgAddFileSource(Pkg* pkg, const char* filename) {
  auto src = memalloct(pkg->mem, Source);
  if (!SourceOpen(src, pkg, filename)) {
    memfree(pkg->mem, src);
    errlog("failed to open %s", filename);
    return false;
  }
  PkgAddSource(pkg, src);
  return true;
}

bool PkgScanSources(Pkg* pkg) {
  assert(pkg->srclist == NULL);
  DIR* dirp = opendir(pkg->dir);
  if (!dirp)
    return false;

  DirEntry e;
  int readdir_status;
  bool ok = true;
  while ((readdir_status = fs_readdir(dirp, &e)) > 0) {
    switch (e.d_type) {
      case DT_REG:
      case DT_LNK:
      case DT_UNKNOWN:
        if (e.d_namlen > 3 && e.d_name[0] != '.' && strcmp(&e.d_name[e.d_namlen-2], ".c") == 0)
          ok = PkgAddFileSource(pkg, e.d_name) && ok;
        break;
      default:
        break;
    }
  }

  return closedir(dirp) == 0 && ok && readdir_status == 0;
}


// -----------------------------------------------------------------------------------------------
// SrcPos

static void computeLineOffsets(Source* s) {
  assert(s->lineoffs == NULL);
  if (!s->body)
    SourceOpenBody(s);

  size_t cap = 256; // best guess for common line numbers, to allocate up-front
  s->lineoffs = (u32*)memalloc(NULL, sizeof(u32) * cap);
  s->lineoffs[0] = 0;

  u32 linecount = 1;
  u32 i = 0;
  while (i < s->len) {
    if (s->body[i++] == '\n') {
      if (linecount == cap) {
        // more lines
        cap = cap * 2;
        s->lineoffs = (u32*)memrealloc(NULL, s->lineoffs, sizeof(u32) * cap);
      }
      s->lineoffs[linecount] = i;
      linecount++;
    }
  }

  s->nlines = linecount;
}


LineCol SrcPosLineCol(SrcPos pos) {
  Source* s = pos.src;
  if (s == NULL) {
    // NoSrcPos
    LineCol lico = { 0, 0 };
    return lico;
  }

  if (!s->lineoffs)
    computeLineOffsets(s);

  if (pos.offs >= s->len) { dlog("pos.offs=%u >= s->len=%zu", pos.offs, s->len); }
  assert(pos.offs < s->len);

  u32 count = s->nlines;
  u32 line = 0;
  u32 debug1 = 10;
  while (count > 0 && debug1--) {
    u32 step = count / 2;
    u32 i = line + step;
    if (s->lineoffs[i] <= pos.offs) {
      line = i + 1;
      count = count - step - 1;
    } else {
      count = step;
    }
  }
  LineCol lico = { line - 1, line > 0 ? pos.offs - s->lineoffs[line - 1] : pos.offs };
  return lico;
}


static const u8* lineContents(Source* s, u32 line, u32* out_len) {
  if (!s->lineoffs)
    computeLineOffsets(s);

  if (line >= s->nlines)
    return NULL;

  auto start = s->lineoffs[line];
  const u8* lineptr = s->body + start;
  if (out_len) {
    if (line + 1 < s->nlines) {
      *out_len = (s->lineoffs[line + 1] - 1) - start;
    } else {
      *out_len = (s->body + s->len) - lineptr;
    }
  }
  return lineptr;
}


Str SrcPosFmtv(SrcPos pos, Str s, const char* fmt, va_list ap) {
  TStyleTable style = TStyle16;

  s = str_appendcstr(s, style[TStyle_bold]);
  s = SrcPosStr(pos, s);
  s = str_appendcstr(s, ": ");
  s = str_appendfmtv(s, fmt, ap);
  s = str_appendcstr(s, style[TStyle_none]);

  // include line contents
  if (pos.src) {
    s = str_appendc(s, '\n');
    u32 linelen;
    auto l = SrcPosLineCol(pos);
    auto lineptr = lineContents(pos.src, l.line, &linelen);
    if (lineptr)
      s = str_append(s, (const char*)lineptr, linelen);
    s = str_appendc(s, '\n');

    // draw a squiggle (or caret when span is unknown) decorating the interesting range
    if (l.col > 0)
      s = str_appendfill(s, l.col, ' '); // indentation
    if (pos.span > 0) {
      s = str_appendfill(s, pos.span, '~'); // squiggle
      s = str_appendc(s, '\n');
    } else {
      s = str_appendcstr(s, "^\n");
    }
  }

  return s;
}


Str SrcPosFmt(SrcPos pos, Str s, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  s = SrcPosFmtv(pos, s, fmt, ap);
  va_end(ap);
  return s;
}


Str SrcPosStr(SrcPos pos, Str s) {
  const char* filename = "<input>";
  size_t filenameLen = strlen("<input>");
  if (pos.src) {
    filename = pos.src->filename;
    filenameLen = strlen(filename);
  }
  auto l = SrcPosLineCol(pos);
  return str_appendfmt(s, "%s:%u:%u", filename, l.line + 1, l.col + 1);
}


// Str SrcPosMsg(SrcPos pos, const char* message) {
//   auto l = SrcPosLineCol(pos);

//   TStyleTable style = TStyle16;

//   Str s = str_fmt("%s%s:%u:%u: %s%s\n",
//     style[TStyle_bold],
//     pos.src ? pos.src->filename : "<input>",
//     l.line + 1,
//     l.col + 1,
//     message ? message : "",
//     style[TStyle_none]
//   );

//   // include line contents
//   if (pos.src) {
//     u32 linelen;
//     auto lineptr = lineContents(pos.src, l.line, &linelen);
//     if (lineptr)
//       s = str_append(s, (const char*)lineptr, linelen);
//     s = str_appendc(s, '\n');

//     // draw a squiggle (or caret when span is unknown) decorating the interesting range
//     if (l.col > 0)
//       s = str_appendfill(s, str_len(s) + l.col, ' '); // indentation
//     if (pos.span > 0) {
//       s = str_appendfill(s, str_len(s) + pos.span + 1, '~'); // squiggle
//       s[str_len(s) - 1] = '\n';
//     } else {
//       s = str_appendcstr(s, "^\n");
//     }
//   }

//   return s;
// }

