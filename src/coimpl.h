// This files contains definitions used across the entire codebase. Keep it lean.
#ifndef CO_IMPL
#define CO_IMPL

#ifndef __cplusplus
  typedef _Bool bool;
  #define true  ((bool)1)
  #define false ((bool)0)
#endif
typedef signed char         i8;
typedef unsigned char       u8;
typedef signed short        i16;
typedef unsigned short      u16;
typedef signed int          i32;
typedef unsigned int        u32;
typedef signed long long    i64;
typedef unsigned long long  u64;
typedef float               f32;
typedef double              f64;
typedef unsigned int        uint;
typedef signed long         isize;
typedef unsigned long       usize;
#ifdef __INTPTR_TYPE__
  typedef __INTPTR_TYPE__   intptr;
  typedef __UINTPTR_TYPE__  uintptr;
#else
  typedef signed long       intptr;
  typedef unsigned long     uintptr;
#endif

#define I8_MAX    0x7f
#define I16_MAX   0x7fff
#define I32_MAX   0x7fffffff
#define I64_MAX   0x7fffffffffffffff
#define ISIZE_MAX __LONG_MAX__

#define I8_MIN    (-0x80)
#define I16_MIN   (-0x8000)
#define I32_MIN   (-0x80000000)
#define I64_MIN   (-0x8000000000000000)
#define ISIZE_MIN (-__LONG_MAX__ -1L)

#define U8_MAX    0xff
#define U16_MAX   0xffff
#define U32_MAX   0xffffffff
#define U64_MAX   0xffffffffffffffff
#define USIZE_MAX (__LONG_MAX__ *2UL+1UL)

#ifdef __INTPTR_MAX__
  #define INTPTR_MIN  (-__INTPTR_MAX__-1L)
  #define INTPTR_MAX  __INTPTR_MAX__
  #define UINTPTR_MAX __UINTPTR_MAX__
#else
  #define INTPTR_MIN  ISIZE_MIN
  #define INTPTR_MAX  ISIZE_MAX
  #define UINTPTR_MAX USIZE_MAX
#endif


// compiler feature test macros
#ifndef __has_attribute
  #define __has_attribute(x)  0
#endif
#ifndef __has_extension
  #define __has_extension   __has_feature
#endif
#ifndef __has_feature
  #define __has_feature(x)  0
#endif
#ifndef __has_include
  #define __has_include(x)  0
#endif
#ifndef __has_builtin
  #define __has_builtin(x)  0
#endif

// nullability
#ifndef NULL
  #define NULL ((void*)0)
#endif
#if __has_feature(nullability)
  #define nonull _Nonnull
#else
  #define nonull _Nonnull
#endif
#if defined(__clang__) && __has_feature(nullability)
  #ifndef nullable
    #define nullable _Nullable
  #endif
  #define ASSUME_NONNULL_BEGIN                                                \
    _Pragma("clang diagnostic push")                                              \
    _Pragma("clang diagnostic ignored \"-Wnullability-completeness\"")            \
    _Pragma("clang diagnostic ignored \"-Wnullability-inferred-on-nested-type\"") \
    _Pragma("clang assume_nonnull begin")
  #define ASSUME_NONNULL_END    \
    _Pragma("clang diagnostic pop") \
    _Pragma("clang assume_nonnull end")
#else
  #ifndef nullable
    #define nullable
  #endif
  #define ASSUME_NONNULL_BEGIN
  #define ASSUME_NONNULL_END
#endif

#ifdef __cplusplus
  #define NORETURN noreturn
#else
  #define NORETURN      _Noreturn
  #define auto          __auto_type
  #define static_assert _Static_assert
#endif

#if __has_attribute(fallthrough)
  #define FALLTHROUGH __attribute__((fallthrough))
#else
  #define FALLTHROUGH
#endif

#if __has_attribute(musttail) && !defined(__wasm__)
  // Note on "!defined(__wasm__)": clang 13 claims to have this attribute for wasm
  // targets but it's actually not implemented and causes an error.
  #define MUSTTAIL __attribute__((musttail))
#else
  #define MUSTTAIL
#endif

#ifndef thread_local
  #define thread_local _Thread_local
#endif

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// ATTR_FORMAT(archetype, string-index, first-to-check)
// archetype determines how the format string is interpreted, and should be printf, scanf,
// strftime or strfmon.
// string-index specifies which argument is the format string argument (starting from 1),
// while first-to-check is the number of the first argument to check against the
// format string. For functions where the arguments are not available to be checked
// (such as vprintf), specify the third parameter as zero.
#if __has_attribute(format)
  #define ATTR_FORMAT(...) __attribute__((format(__VA_ARGS__)))
#else
  #define ATTR_FORMAT(...)
#endif

#if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
  #define ALWAYS_INLINE inline
#endif

#if __has_attribute(noinline)
  #define NO_INLINE __attribute__((noinline))
#else
  #define NO_INLINE
#endif

#if __has_attribute(unused)
  #define UNUSED __attribute__((unused))
#else
  #define UNUSED
#endif

#if __has_attribute(used)
  #define ATTR_USED __attribute__((used))
#else
  #define ATTR_USED
#endif

#if __has_attribute(warn_unused_result)
  #define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
  #define WARN_UNUSED_RESULT
#endif

#if __has_attribute(__packed__)
  #define ATTR_PACKED __attribute__((__packed__))
#else
  #define ATTR_PACKED
#endif

#if __has_attribute(malloc)
  #define ATTR_MALLOC __attribute__((malloc))
#else
  #define ATTR_MALLOC
#endif
#if __has_attribute(alloc_size)
  // void *my_malloc(int a) ATTR_ALLOC_SIZE(1);
  // void *my_calloc(int a, int b) ATTR_ALLOC_SIZE(1, 2);
  #define ATTR_ALLOC_SIZE(args...) __attribute__((alloc_size(args)))
#else
  #define ATTR_ALLOC_SIZE(...)
#endif

#if __has_feature(address_sanitizer)
  // https://clang.llvm.org/docs/AddressSanitizer.html
  #define ASAN_ENABLED 1
  #define ASAN_DISABLE_ADDR_ATTR __attribute__((no_sanitize("address")))
#else
  #define ASAN_DISABLE_ADDR_ATTR
#endif

#if __has_attribute(no_sanitize)
  #define ATTR_NOSAN(str) __attribute__((no_sanitize(str)))
#else
  #define ATTR_NOSAN(str) __attribute__((no_sanitize(str)))
#endif

// _Noreturn abort()
#ifndef CO_NO_LIBC
  void abort(void); // stdlib.h
#elif __has_builtin(__builtin_trap)
  #define abort __builtin_trap
#elif __has_builtin(__builtin_unreachable)
  #define abort __builtin_unreachable()
#else
  #error no abort()
#endif

#if __has_builtin(__builtin_unreachable)
  #define UNREACHABLE __builtin_unreachable()
#elif __has_builtin(__builtin_trap)
  #define UNREACHABLE __builtin_trap
#else
  #define UNREACHABLE abort()
#endif

// UNLIKELY(integralexpr)->bool
#if __has_builtin(__builtin_expect)
  #define LIKELY(x)   (__builtin_expect((bool)(x), true))
  #define UNLIKELY(x) (__builtin_expect((bool)(x), false))
#else
  #define LIKELY(x)   (x)
  #define UNLIKELY(x) (x)
#endif

#if defined(__clang__) || defined(__gcc__)
  #define _DIAGNOSTIC_IGNORE_PUSH(x)  _Pragma("GCC diagnostic push") _Pragma(#x)
  #define DIAGNOSTIC_IGNORE_PUSH(STR) _DIAGNOSTIC_IGNORE_PUSH(GCC diagnostic ignored STR)
  #define DIAGNOSTIC_IGNORE_POP()     _Pragma("GCC diagnostic pop")
#else
  #define DIAGNOSTIC_IGNORE_PUSH(STR)
  #define DIAGNOSTIC_IGNORE_POP()
#endif

#ifndef offsetof
  #if __has_builtin(__builtin_offsetof)
    #define offsetof __builtin_offsetof
  #else
    #define offsetof(st, m) ((usize)&(((st*)0)->m))
  #endif
#endif

#ifndef alignof
  #define alignof _Alignof
#endif

#ifndef alignas
  #define alignas _Alignas
#endif

#ifndef countof
  #define countof(x) \
    ((sizeof(x)/sizeof(0[x])) / ((usize)(!(sizeof(x) % sizeof(0[x])))))
#endif

#define CONCAT_(x,y) x##y
#define CONCAT(x,y)  CONCAT_(x,y)

#define MAX(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

#define MIN(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

// SET_FLAG(int flags, int flag, bool on)
// equivalent to: if (on) flags |= flag; else flags &= ~flag
#define SET_FLAG(flags, flag, on) (flags ^= (-(!!(on)) ^ (flags)) & (flag))

// T ALIGN2<T>(T x, anyuint a)       rounds up x to nearest a (a must be a power of two)
// T ALIGN2_FLOOR<T>(T x, anyuint a) rounds down x to nearest a
// bool IS_ALIGN2(T x, anyuint a)    true if x is aligned to a
#define ALIGN2(x,a)           _ALIGN2_MASK(x, (__typeof__(x))(a) - 1)
#define ALIGN2_FLOOR(x, a)    ALIGN2((x) - ((a) - 1), (a))
#define IS_ALIGN2(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define _ALIGN2_MASK(x, mask) (((x) + (mask)) & ~(mask))

// co_ctz returns the number of trailing 0-bits in x,
// starting at the least significant bit position. If x is 0, the result is undefined.
#define co_ctz(x) _Generic((x), \
  u32:   __builtin_ctz,       \
  usize: __builtin_ctzl,      \
  u64:   __builtin_ctzll)(x)

// END_ENUM(NAME) should be placed at an enum that has a matching integer typedef.
// Example 1:
//   typedef u16 foo;
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_ENUM(foo);
//   // ok; no error since fooC fits in u16
// Example 2:
//   typedef u8 foo; // too small for fooC value
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_ENUM(foo);
//   // error: static_assert failed due to requirement
//   // 'sizeof(enum foo) <= sizeof(unsigned char)' "too many foo values"
//
#if __has_attribute(__packed__)
  #define END_ENUM(NAME) \
    __attribute__((__packed__));  \
    static_assert(sizeof(enum NAME) <= sizeof(NAME), "too many " #NAME " values");
#else
  #define END_ENUM(NAME) ;
#endif

// u32 CAST_U32(anyint z) => [0-U32_MAX]
#define CAST_U32(z) ({ \
  __typeof__(z) z__ = (z); \
  sizeof(u32) < sizeof(z__) ? (u32)MIN((__typeof__(z__))U32_MAX,z__) : (u32)z__; \
})

// __fls(uint n) finds the last (most-significant) bit set
#define __fls(n) ((sizeof(n) <= 4) ? __fls32(n) : __fls64(n))
static ALWAYS_INLINE int __fls32(unsigned int x) {
  return x ? sizeof(x) * 8 - __builtin_clz(x) : 0;
}
static ALWAYS_INLINE unsigned long __flsl(unsigned long x) {
  return (sizeof(x) * 8) - 1 - __builtin_clzl(x);
}
#if USIZE_MAX < 0xffffffffffffffff
  static ALWAYS_INLINE int __fls64(u64 x) {
    u32 h = x >> 32;
    if (h)
      return __fls32(h) + 32;
    return __fls32(x);
  }
#else
  static ALWAYS_INLINE int __fls64(u64 x) {
    if (x == 0)
      return 0;
    return __flsl(x) + 1;
  }
#endif

static inline WARN_UNUSED_RESULT bool __must_check_unlikely(bool unlikely) {
  return UNLIKELY(unlikely);
}


// —————————————————————————————————————————————————————————————————————————————————————
// BEGIN code adapted from Linux.
// The code listed on the following lines up until "END code adapted from Linux"
// is licensed under the MIT license: (<linux-src>/LICENSES/preferred/MIT)
//
// MIT License
//
// Copyright (c) <year> <copyright holders>
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#define __same_type(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))

// #define _ALIGN1(x, a)          _ALIGN1_MASK(x, (__typeof__(x))(a) - 1)
// #define _ALIGN1_MASK(x, mask)  (((x) + (mask)) & ~(mask))
// #define ALIGN(x, a)            _ALIGN1((x), (a))
// #define ALIGN_DOWN(x, a)       _ALIGN1((x) - ((a) - 1), (a))
// #define PTR_ALIGN(p, a)        ((__typeof__(p))ALIGN((unsigned long)(p), (a)))
// #define PTR_ALIGN_DOWN(p, a)   ((__typeof__(p))ALIGN_DOWN((unsigned long)(p), (a)))
// #define IS_ALIGNED(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)

// a + b => d
#define check_add_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_add_overflow(__a, __b, __d);  \
}))

// a - b => d
#define check_sub_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_sub_overflow(__a, __b, __d);  \
}))

// a * b => d
#define check_mul_overflow(a, b, d) __must_check_unlikely(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_mul_overflow(__a, __b, __d);  \
}))

// Compute a*b+c, returning USIZE_MAX on overflow. Internal helper for STRUCT_SIZE() below.
static inline WARN_UNUSED_RESULT usize __ab_c_size(usize a, usize b, usize c) {
  usize bytes;
  if (check_mul_overflow(a, b, &bytes))
    return USIZE_MAX;
  if (check_add_overflow(bytes, c, &bytes))
    return USIZE_MAX;
  return bytes;
}

// STRUCT_SIZE calculates size of structure with trailing array, checking for overflow.
// p      Pointer to the structure
// member Name of the array member
// count  Number of elements in the array
//
// Calculates size of memory needed for structure p followed by an array of count number
// of member elements.
// Returns number of bytes needed or USIZE_MAX on overflow.
#define STRUCT_SIZE(p, member, count)                    \
  __ab_c_size(count,                                     \
    sizeof(*(p)->member) + __must_be_array((p)->member), \
    sizeof(*(p)))

// array_size calculates size of 2-dimensional array (i.e. a * b)
// Returns number of bytes needed to represent the array or USIZE_MAX on overflow.
static inline WARN_UNUSED_RESULT usize array_size(usize a, usize b) {
  usize bytes;
  if (check_mul_overflow(a, b, &bytes))
    return USIZE_MAX;
  return bytes;
}

// BUILD_BUG_ON_ZERO is a neat trick used in the Linux kernel source to force a
// compilation error if condition is true, but also produce a result
// (of value 0 and type int), so the expression can be used e.g. in a structure
// initializer (or where-ever else comma expressions aren't permitted).
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))

#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

// ARRAY_LEN: number of elements of an array
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

// ILOG2 calculates the log of base 2
#define ILOG2(n) ( \
  __builtin_constant_p(n) ? ((n) < 2 ? 0 : 63 - __builtin_clzll(n)) : __fls(n) - 1 )

// CEIL_POW2 rounds up n to nearest power of two. Result is undefined when n is 0.
#define CEIL_POW2(n) ( \
  __builtin_constant_p(n) ? ( ((n) == 1) ? 1 : (1UL << (ILOG2((n) - 1) + 1)) ) \
                          : (1UL << __fls(n - 1)) )

// END code adapted from Linux
// —————————————————————————————————————————————————————————————————————————————————————

#define BEGIN_INTERFACE \
  ASSUME_NONNULL_BEGIN \
  DIAGNOSTIC_IGNORE_PUSH("-Wunused-function")

#define END_INTERFACE \
  DIAGNOSTIC_IGNORE_POP() \
  ASSUME_NONNULL_END

// assume pointer types are "nonull"
ASSUME_NONNULL_BEGIN

// —————————————————————————————————————————————————————————————————————————————————————
// error

typedef i32 error;
#define CO_FOREACH_ERROR(_) \
  _(ok            , "(no error)") \
  _(invalid       , "invalid data or argument") \
  _(sys_op        , "invalid syscall op or syscall op data") \
  _(badfd         , "invalid file descriptor") \
  _(bad_name      , "invalid or misformed name") \
  _(not_found     , "not found") \
  _(name_too_long , "name too long") \
  _(canceled      , "operation canceled") \
  _(not_supported , "not supported") \
  _(exists        , "already exists") \
  _(access        , "permission denied") \
  _(nomem         , "cannot allocate memory") \
  _(mfault        , "bad memory address") \
  _(overflow      , "value too large") \
// end CO_FOREACH_ERROR
enum _co_error_tmp_ { // generate positive values
  #define _(NAME, ...) _err_##NAME,
  CO_FOREACH_ERROR(_)
  #undef _
};
enum error { // canonical negative values
  #define _(NAME, ...) err_##NAME = - _err_##NAME,
  CO_FOREACH_ERROR(_)
  #undef _
} END_ENUM(error)

error error_from_errno(int errno);
const char* error_str(error);

// —————————————————————————————————————————————————————————————————————————————————————
// panic & assert

// panic prints msg to stderr and calls abort()
#define panic(fmt, args...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##args)

NORETURN void _panic(const char* file, int line, const char* fun, const char* fmt, ...)
  ATTR_FORMAT(printf, 4, 5);

// void log(const char* fmt, ...)
#ifdef CO_NO_LIBC
  #warning log not implemented for no-libc
  #define log(format, ...) ((void)0)
#else
  ASSUME_NONNULL_END
  #include <stdio.h>
  ASSUME_NONNULL_BEGIN
  #define log(format, args...) fprintf(stderr, format "\n", ##args)
#endif

// void errlog(const char* fmt, ...)
#define errlog(format, args...) ({                              \
  log("error: " format " (%s:%d)", ##args, __FILE__, __LINE__); \
  fflush(stderr); })

// void assert(expr condition)
#undef assert
#if defined(DEBUG) || !defined(NDEBUG)
  #undef DEBUG
  #undef NDEBUG
  #undef CO_SAFE
  #define DEBUG 1
  #define CO_SAFE 1

  #define _assertfail(fmt, args...) \
    _panic(__FILE__, __LINE__, __FUNCTION__, "Assertion failed: " fmt, args)
  // Note: we can't use ", ##args" above in either clang nor gcc for some reason,
  // or else certain applications of this macro are not expanded.

  #define assertf(cond, fmt, args...) \
    if (UNLIKELY(!(cond))) _assertfail(fmt " (%s)", ##args, #cond)

  #define assert(cond) \
    if (UNLIKELY(!(cond))) _assertfail("%s", #cond)

  #define assertop(a,op,b) ({                                               \
    __typeof__(a) A__ = a;                                                  \
    __typeof__(a) B__ = b; /* intentionally typeof(a) and not b for lits */ \
    if (UNLIKELY(!(A__ op B__)))                                            \
      _assertfail("%s %s %s (%s %s %s)",                                    \
        #a, #op, #b, debug_quickfmt(0,A__), #op, debug_quickfmt(1,B__));    \
  })

  #define assertcstreq(cstr1, cstr2) ({                  \
    const char* cstr1__ = (cstr1);                       \
    const char* cstr2__ = (cstr2);                       \
    if (UNLIKELY(strcmp(cstr1__, cstr2__) != 0))         \
      _assertfail("\"%s\" != \"%s\"", cstr1__, cstr2__); \
  })

  #define asserteq(a,b)    assertop((a),==,(b))
  #define assertne(a,b)    assertop((a),!=,(b))
  #define assertlt(a,b)    assertop((a),<, (b))
  #define assertgt(a,b)    assertop((a),>, (b))
  #define assertnull(a)    assertop((a),==,NULL)

  #define assertnotnull(a) ({                                         \
    __typeof__(a) val__ = (a);                                        \
    UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
    if (UNLIKELY(val__ == NULL))                                      \
      _assertfail("%s != NULL", #a);                                  \
    val__; })

#else /* !defined(NDEBUG) */
  #undef DEBUG
  #undef NDEBUG
  #define NDEBUG 1
  #define assert(cond)            ((void)0)
  #define assertf(cond, fmt, ...) ((void)0)
  #define assertop(a,op,b)        ((void)0)
  #define assertcstreq(a,b)       ((void)0)
  #define asserteq(a,b)           ((void)0)
  #define assertne(a,b)           ((void)0)
  #define assertlt(a,b)           ((void)0)
  #define assertgt(a,b)           ((void)0)
  #define assertnull(a)           ((void)0)
  #define assertnotnull(a)        ({ a; }) /* note: (a) causes "unused" warnings */
#endif /* !defined(NDEBUG) */


// CO_SAFE -- checks enabled in "debug" and "safe" builds (but not in "fast" builds.)
//
// void safecheck(COND)                        -- elided from non-safe builds
// void safecheckf(COND, const char* fmt, ...) -- elided from non-safe builds
// EXPR safecheckexpr(EXPR, EXPECT)            -- included in non-safe builds (without check)
// typeof(EXPR) safenotnull(EXPR)              -- included in non-safe builds (without check)
//
#ifdef CO_SAFE
  #undef CO_SAFE
  #define CO_SAFE 1
  #define _safefail(fmt, args...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##args)
  #define safecheckf(cond, fmt, args...) if UNLIKELY(!(cond)) _safefail(fmt, ##args)
  #ifdef DEBUG
    #define safecheck(cond) if UNLIKELY(!(cond)) _safefail("safecheck (%s)", #cond)
    #define safecheckexpr(expr, expect) ({                                        \
      __typeof__(expr) val__ = (expr);                                            \
      safecheckf(val__ == expect, "unexpected value (%s != %s)", #expr, #expect); \
      val__; })
    #define safenotnull(a) ({                                           \
      __typeof__(a) val__ = (a);                                        \
      UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
      safecheckf(val__ != NULL, "unexpected NULL (%s)", #a);            \
      val__; })
  #else
    #define safecheck(cond) if UNLIKELY(!(cond)) _safefail("safecheck")
    #define safecheckexpr(expr, expect) ({ \
      __typeof__(expr) val__ = (expr); safecheck(val__ == expect); val__; })
    #define safenotnull(a) ({                                           \
      __typeof__(a) val__ = (a);                                        \
      UNUSED const void* valp__ = val__; /* build bug on non-pointer */ \
      safecheckf(val__ != NULL, "NULL");                                \
      val__; })
  #endif
#else
  #define safecheckf(cond, fmt, args...) ((void)0)
  #define safecheck(cond)                ((void)0)
  #define safecheckexpr(expr, expect)    (expr) /* intentionally complain if not used */
  #define safenotnull(a)                 ({ a; }) /* note: (a) causes "unused" warnings */
#endif

// void dlog(const char* fmt, ...)
#ifdef DEBUG
  // debug_quickfmt formats a value x and returns a temporary string for use in printing.
  // The buffer argument should be a number in the inclusive range [0-5], determining which
  // temporary buffer to use and return a pointer to.
  #define debug_quickfmt(buffer, x) debug_tmpsprintf(buffer, _Generic((x), \
    unsigned long long: "%llu", \
    unsigned long:      "%lu", \
    unsigned int:       "%u", \
    unsigned short:     "%u", \
    long long:          "%lld", \
    long:               "%ld", \
    int:                "%d", \
    short:              "%d", \
    char:               "%c", \
    unsigned char:      "%C", \
    const char*:        "%s", \
    char*:              "%s", \
    bool:               "%d", \
    float:              "%f", \
    double:             "%f", \
    void*:              "%p", \
    const void*:        "%p", \
    default:            "%p" \
  ), x)
  // debug_tmpsprintf is like sprintf but uses a static buffer.
  // The buffer argument determines which buffer to use (constraint: buffer<6)
  const char* debug_tmpsprintf(int buffer, const char* fmt, ...)
    ATTR_FORMAT(printf, 2, 3);
  #ifdef CO_NO_LIBC
    #define dlog(format, args...) \
      log("[D] " format " (%s:%d)", ##args, __FILE__, __LINE__)
  #else
    ASSUME_NONNULL_END
    #include <unistd.h> // isatty
    ASSUME_NONNULL_BEGIN
    #define dlog(format, args...) ({                                 \
      if (isatty(2)) log("\e[1;35m▍\e[0m" format " \e[2m%s:%d\e[0m", \
                         ##args, __FILE__, __LINE__);                \
      else           log("[D] " format " (%s:%d)",                   \
                         ##args, __FILE__, __LINE__);                \
      fflush(stderr); })
  #endif
#else
  #define dlog(format, ...) ((void)0)
#endif


// —————————————————————————————————————————————————————————————————————————————————————

#define fabs   __builtin_fabs
#define sinf   __builtin_sinf
#define cosf   __builtin_cosf
#define floor  __builtin_floor
#define ceil   __builtin_ceil

#define memset  __builtin_memset
#define memcpy  __builtin_memcpy
#define memcmp  __builtin_memcmp
#define memchr  __builtin_memchr
#define memmove __builtin_memmove

#define strlen __builtin_strlen
#define strcmp __builtin_strcmp

typedef __builtin_va_list va_list;
#ifndef va_start
  #define va_start __builtin_va_start
  #define va_end   __builtin_va_end
  #define va_arg   __builtin_va_arg
  #define va_copy  __builtin_va_copy
#endif

char* strstr(const char* haystack, const char* needle);

// xqsort is qsort_r aka qsort_s
typedef int(*xqsort_cmp)(const void* x, const void* y, void* nullable ctx);
void xqsort(void* base, usize nmemb, usize size, xqsort_cmp cmp, void* nullable ctx);

// —————————————————————————————————————————————————————————————————————————————————————

typedef struct SSlice { const char* p; usize len; } SSlice; // TODO: rename to Str
#define SSLICE(cstr) ((SSlice){ (cstr), strlen(cstr) })

// —————————————————————————————————————————————————————————————————————————————————————
ASSUME_NONNULL_END
#endif // CO_IMPL
