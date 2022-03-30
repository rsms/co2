#include "colib.h"

#ifndef CO_NO_LIBC
  #include <errno.h>
  #include <time.h>
  #include <sys/time.h>
  #if defined __APPLE__
    #include <mach/mach_time.h>
  #endif
#endif


#if defined(__APPLE__)
  // fraction to multiply a value in mach tick units with to convert it to nanoseconds
  static mach_timebase_info_data_t tbase;
#endif


error unixtime(i64* sec, u64* nsec) {
  #ifdef CLOCK_REALTIME
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts))
      return error_from_errno(errno);
    *sec = (i64)ts.tv_sec;
    *nsec = (u64)ts.tv_nsec;
    return 0;
  #elif !defined(CO_NO_LIBC)
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0)
      return error_from_errno(errno);
    *sec = (i64)tv.tv_sec;
    *nsec = ((u64)tv.tv_usec) * 1000;
    return 0;
  #else
    #warning TODO CO_NO_LIBC unixtime
    return err_not_supported;
  #endif
}

u64 nanotime(void) {
  #if defined(__APPLE__)
    u64 t = mach_absolute_time();
    return (t * tbase.numer) / tbase.denom;
  #elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    safecheckexpr(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
    return ((u64)(ts.tv_sec) * 1000000000) + ts.tv_nsec;
  // TODO #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
  //   QueryPerformanceCounter
  #elif !defined(CO_NO_LIBC)
    struct timeval tv;
    safecheckexpr(gettimeofday(&tv, nullptr), 0);
    return ((u64)(tv.tv_sec) * 1000000000) + ((u64)(tv.tv_usec) * 1000);
  #else
    #warning TODO CO_NO_LIBC nanotime
    return 0;
  #endif
}

usize fmtduration(char buf[25], u64 duration_ns) {
  // max value: "18446744073709551615.1ms\0"
  const char* unit = "ns";
  u64 d = duration_ns;
  u64 f = 0;
  if (duration_ns >= 1000000000) {
    f = d % 1000000000;
    d /= 1000000000;
    unit = "s\0";
  } else if (duration_ns >= 1000000) {
    f = d % 1000000;
    d /= 1000000;
    unit = "ms";
  } else if (duration_ns >= 1000) {
    d /= 1000;
    unit = "us\0";
  }
  usize i = sfmt_u64(buf, d, 10);
  if (unit[0] != 'u' && unit[0] != 'n') {
    // one decimal for units larger than microseconds
    buf[i++] = '.';
    char buf2[20];
    UNUSED usize n = sfmt_u64(buf2, f, 10);
    assert(n > 0);
    buf[i++] = buf2[0]; // TODO: round instead of effectively ceil
  }
  buf[i++] = unit[0];
  buf[i++] = unit[1];
  buf[i] = 0;
  return i;
}


u64 microsleep(u64 microseconds) {
  #ifndef CO_NO_LIBC
    u64 sec = microseconds / 1000000;
    struct timespec request = {
      .tv_sec  = (long)sec,
      .tv_nsec = (microseconds - sec*1000000) * 1000,
    };
    struct timespec remaining = {0};
    if (nanosleep(&request, &remaining) != 0)
      return remaining.tv_sec*1000000 + remaining.tv_nsec/1000;
  #else
    #warning TODO non-libc microsleep
  #endif
  return 0;
}


void _logtime_end(const TimeLabel* t) {
  u64 timespent = nanotime() - t->ns;
  char durbuf[25];
  fmtduration(durbuf, timespent);
  log("◔ %s %s", t->label, durbuf);
}


error time_init() {
  #if defined(__APPLE__)
    if (mach_timebase_info(&tbase) != KERN_SUCCESS)
      return err_not_supported;
  #endif
  return 0;
}
