// time
#pragma once
ASSUME_NONNULL_BEGIN

// unixtime returns the current real time as a UNIX timestamp, i.e. number of seconds
// since Jan 1 1970 UTC. Returns I64_MIN if the underlying system time query fails.
i64 unixtime();

// unixtimef returns the current real time as a UNIX timestamp (high precision.)
// Returns NAN if the underlying system time query fails.
double unixtimef();

// unixtime2 returns the second and nanasecond parts as two integers.
error unixtime2(i64* sec, u64* nsec);

// nanotime returns nanoseconds measured from an undefined point in time.
// It uses the most high-resolution, low-latency clock available on the system.
u64 nanotime();

// fmtduration appends human-readable time duration to buf, including a null terminator.
// Returns number of bytes written, excluding the null terminator.
u32 fmtduration(char buf[25], u64 duration_ns);

// microsleep sleeps for some number of microseconds.
// Returns amount of time remaining if the thread was interrupted
// or 0 if the thread slept for at least microseconds.
u64 microsleep(u64 microseconds);

// logtime -- measure real time spent between two points of execution.
//   TimeLabel logtime_start(const char* label)
//   void      logtime_end(const TimeLabel)
//   void      logtime_scope(const char* label)
//
typedef struct { const char* label; u64 ns; } TimeLabel;
inline static TimeLabel logtime_start(const char* label) {
  return (TimeLabel){label,nanotime()}; }
#define logtime_end(t) _logtime_end(&(t))
void _logtime_end(const TimeLabel* t);
#if __has_attribute(__cleanup__)
  #define logtime_scope(label) \
    UNUSED TimeLabel logtime__ __attribute__((__cleanup__(_logtime_end))) = logtime_start(label)
#else
  #define logtime_scope(label) ((void)0)
#endif

ASSUME_NONNULL_END
