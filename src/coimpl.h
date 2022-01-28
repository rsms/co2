// This files contains definitions used across the entire codebase. Keep it lean.
#pragma once

#ifndef __cplusplus
  typedef _Bool bool;
  #define true  ((bool)1)
  #define false ((bool)0)
#endif
typedef signed char            i8;
typedef unsigned char          u8;
typedef signed short int       i16;
typedef unsigned short int     u16;
typedef signed int             i32;
typedef unsigned int           u32;
typedef signed long long int   i64;
typedef unsigned long long int u64;
typedef float                  f32;
typedef double                 f64;
typedef unsigned int           uint;
typedef signed long            isize;
typedef unsigned long          usize;
typedef signed long            intptr;
typedef unsigned long          uintptr;

#ifndef NULL
  #define NULL ((void*)0)
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
#if defined(__clang__) && __has_feature(nullability)
  #define __NULLABILITY_PRAGMA_PUSH \
    _Pragma("clang diagnostic push")  \
    _Pragma("clang diagnostic ignored \"-Wnullability-completeness\"") \
    _Pragma("clang diagnostic ignored \"-Wnullability-inferred-on-nested-type\"")

  #define __NULLABILITY_PRAGMA_POP \
    _Pragma("clang diagnostic pop")

  #define ASSUME_NONNULL_BEGIN \
    __NULLABILITY_PRAGMA_PUSH  \
    _Pragma("clang assume_nonnull begin")

  #define ASSUME_NONNULL_END \
    __NULLABILITY_PRAGMA_POP \
    _Pragma("clang assume_nonnull end")
#else
  #define _Nullable
  #define _Nonnull
  #define _Null_unspecified
  #define __NULLABILITY_PRAGMA_PUSH
  #define __NULLABILITY_PRAGMA_POP
  #define ASSUME_NONNULL_BEGIN
  #define ASSUME_NONNULL_END
#endif
#define nullable      _Nullable
#define nonull        _Nonnull
#define nonnullreturn __attribute__((returns_nonnull))

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

#if __has_attribute(musttail)
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
  #define USED __attribute__((used))
#else
  #define USED
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
  #define ATTR_ALLOC_SIZE(whicharg) __attribute__((alloc_size(whicharg)))
#else
  #define ATTR_ALLOC_SIZE(whicharg)
#endif

#if __has_feature(address_sanitizer)
  // https://clang.llvm.org/docs/AddressSanitizer.html
  #define ASAN_ENABLED 1
  #define ASAN_DISABLE_ADDR_ATTR __attribute__((no_sanitize("address")))
#else
  #define ASAN_DISABLE_ADDR_ATTR
#endif

#ifdef CO_WITH_LIBC
  void abort(void); // stdlib.h
#elif __has_builtin(__builtin_trap)
  #define abort __builtin_trap
#else
  #error no abort()
#endif

#if __has_builtin(__builtin_trap)
  #define TRAP __builtin_trap
#else
  #define TRAP abort
#endif

#if __has_builtin(__builtin_unreachable)
  #define UNREACHABLE __builtin_unreachable()
#else
  #define UNREACHABLE TRAP()
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

// UNLIKELY(integralexpr)->integralexpr
#if __has_builtin(__builtin_expect)
  #define UNLIKELY(x) __builtin_expect((x), 0)
  #define LIKELY(x)   __builtin_expect((x), 1)
#else
  #define UNLIKELY(x) (x)
  #define LIKELY(x)   (x)
#endif

// T ALIGN2<T>(T x, anyuint a)       rounds up x to nearest a (a must be a power of two)
// T ALIGN2_FLOOR<T>(T x, anyuint a) rounds down x to nearest a
// bool IS_ALIGN2(T x, anyuint a)    true if x is aligned to a
#define ALIGN2(x,a)           _ALIGN2_MASK(x, (__typeof__(x))(a) - 1)
#define ALIGN2_DOWN(x, a)     ALIGN2((x) - ((a) - 1), (a))
#define IS_ALIGN2(x, a)       (((x) & ((__typeof__(x))(a) - 1)) == 0)
#define _ALIGN2_MASK(x, mask) (((x) + (mask)) & ~(mask))

// END_TYPED_ENUM(NAME) should be placed at an enum that has a matching integer typedef.
// Example 1:
//   typedef u16 foo;
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_TYPED_ENUM(foo);
//   // ok; no error since fooC fits in u16
// Example 2:
//   typedef u8 foo; // too small for fooC value
//   enum foo { fooA, fooB = 0xff, fooC = 0xfff } END_TYPED_ENUM(foo);
//   // error: static_assert failed due to requirement
//   // 'sizeof(enum foo) <= sizeof(unsigned char)' "too many foo values"
//
#if __has_attribute(__packed__)
  #define END_TYPED_ENUM(NAME)   \
    __attribute__((__packed__)); \
    static_assert(sizeof(enum NAME) <= sizeof(NAME), "too many " #NAME " values");
#else
  #define END_TYPED_ENUM(NAME) ;
#endif


// ======================================================================================
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

static inline WARN_UNUSED_RESULT bool __must_check_overflow(bool overflow) {
  return UNLIKELY(overflow);
}

// a + b => d
#define check_add_overflow(a, b, d) __must_check_overflow(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_add_overflow(__a, __b, __d);  \
}))

// a - b => d
#define check_sub_overflow(a, b, d) __must_check_overflow(({  \
  __typeof__(a) __a = (a);      \
  __typeof__(b) __b = (b);      \
  __typeof__(d) __d = (d);      \
  (void) (&__a == &__b);      \
  (void) (&__a == __d);     \
  __builtin_sub_overflow(__a, __b, __d);  \
}))

// a * b => d
#define check_mul_overflow(a, b, d) __must_check_overflow(({  \
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

// __fls finds the last (most-significant) bit set
#define __fls(x) (x ? sizeof(x) * 8 - __builtin_clz(x) : 0)

// ILOG2 calculates the log of base 2
#define ILOG2(n) (             \
  __builtin_constant_p(n) ?    \
    ((n) < 2 ? 0 :             \
    63 - __builtin_clzll(n)) : \
  __fls(n)                     \
)

// CEIL_POW2 rounds up n to nearest power of two. Result is undefined when n is 0.
#define CEIL_POW2(n) (              \
  __builtin_constant_p(n) ? (       \
    ((n) == 1) ? 1 :                \
      (1UL << (ILOG2((n) - 1) + 1)) \
    ) :                             \
    (1UL << __fls(n - 1))           \
)

// END code adapted from Linux
// ======================================================================================

// assume pointer types are "nonull"
ASSUME_NONNULL_BEGIN

// ======================================================================================
// error

typedef i32 error;
enum error {
  err_ok            =   0, // no error
  err_invalid       =  -1, // invalid data or argument
  err_sys_op        =  -2, // invalid syscall op or syscall op data
  err_badfd         =  -3, // invalid file descriptor
  err_bad_name      =  -4, // invalid or misformed name
  err_not_found     =  -5, // resource not found
  err_name_too_long =  -6, // name too long
  err_canceled      =  -7, // operation canceled
  err_not_supported =  -8, // not supported
  err_exists        =  -9, // already exists
  err_end           = -10, // end of resource
  err_access        = -11, // permission denied
  err_nomem         = -12, // cannot allocate memory
  err_mfault        = -13, // bad memory address
  err_overflow      = -14, // value too large for defined data type
};

error error_from_errno(int errno);
const char* error_str(error);

// ======================================================================================
// panic & assert

// panic prints msg to stderr and calls TRAP()
#define panic(fmt, ...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

NORETURN void _panic(const char* file, int line, const char* fun, const char* fmt, ...)
  ATTR_FORMAT(printf, 4, 5);

// void assert(expr condition)
#undef assert
#if defined(DEBUG) || !defined(NDEBUG)
  #undef NDEBUG

  #define _assertfail(fmt, args...) \
    _panic(__FILE__, __LINE__, __FUNCTION__, "Assertion failed: " fmt, args)
  // Note: we can't use ", ##args" above in either clang nor gcc for some reason,
  // or else certain applications of this macro are not expanded.

  #define assertf(cond, fmt, ...) \
    if (UNLIKELY(!(cond))) _assertfail("%s; " fmt, #cond, ##__VA_ARGS__)

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

  #define assertnotnull(a) ({        \
    __typeof__(a) val__ = (a);       \
    if (UNLIKELY(val__ == NULL))     \
      _assertfail("%s != NULL", #a); \
    val__; })

#else /* !defined(NDEBUG) */
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
  const char* debug_tmpsprintf(int buffer, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
#else
  #define debug_quickfmt(...) ""
  #define debug_tmpsprintf(...) ""
#endif // defined(DEBUG)

// void dlog(const char* fmt, ...)
#ifdef DEBUG
  #ifndef CO_WITH_LIBC
    #warning dlog not implemented for no-libc
    #define dlog(format, ...) ((void)0)
  #else
    ASSUME_NONNULL_END
    #include <stdio.h>
    ASSUME_NONNULL_BEGIN
    #define dlog(format, ...) ({ \
      fprintf(stderr, "\e[1;34m[D]\e[0m " format " \e[2m(%s %d)\e[0m\n", \
        ##__VA_ARGS__, __FUNCTION__, __LINE__); \
      fflush(stderr); \
    })
  #endif
#else
  #define dlog(format, ...) ((void)0)
#endif

// void errlog(const char* fmt, ...)
#ifdef CO_WITH_LIBC
  ASSUME_NONNULL_END
  #include <stdio.h>
  ASSUME_NONNULL_BEGIN
  #define errlog(format, ...) (({ \
    fprintf(stderr, "error: " format " (%s:%d)\n", \
      ##__VA_ARGS__, __FILE__, __LINE__); \
    fflush(stderr); \
  }))
#else
  #warning errlog not implemented for no-libc
  #define errlog(format, ...) ((void)0)
#endif


// ======================================================================================
// libc host-independent functions (just the parts we need)

#define fabs   __builtin_fabs
#define sinf   __builtin_sinf
#define cosf   __builtin_cosf
#define floor  __builtin_floor
#define ceil   __builtin_ceil

#define memset __builtin_memset
#define memcpy __builtin_memcpy
#define memcmp __builtin_memcmp
#define memchr __builtin_memchr
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


// ======================================================================================
ASSUME_NONNULL_END
