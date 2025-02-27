#ifndef __TIMER_H__
#define __TIMER_H__

// implementation: none

#include "stdafx.h"
#include <stdbool.h>
#include <sys/time.h>

NOVA_HEADER_START

#ifndef NV_TIMER_TIME_TYPE
#  define NV_TIMER_TIME_TYPE real_t
#endif

typedef struct timer
{
  NV_TIMER_TIME_TYPE m_start;
  NV_TIMER_TIME_TYPE m_end;
} timer;

static inline NV_TIMER_TIME_TYPE
_nv_timer_get_currtime(void)
{
  struct timeval tval;
  if (gettimeofday(&tval, 0))
  {
    return __FLT_MAX__;
  }
  const NV_TIMER_TIME_TYPE micro_seconds_to_seconds = 1.0 / 1000000.0;
  return (NV_TIMER_TIME_TYPE)tval.tv_sec + ((NV_TIMER_TIME_TYPE)tval.tv_usec * micro_seconds_to_seconds);
}

// the timer will finish after 's' seconds
// pass __FLT_MAX__ for duration for it to just not end
static inline timer
nv_timer_begin(NV_TIMER_TIME_TYPE duration_seconds)
{
  if (duration_seconds <= 0.0F)
  {
    nv_push_error("Timer duration passed as negative or zero. What do you even want the "
                  "timer to do????");
    return (timer){ -1, -1 };
  }
  timer tmr;
  tmr.m_start = _nv_timer_get_currtime();
  tmr.m_end   = tmr.m_start + duration_seconds;
  return tmr;
}

// sets the end of the timer to -1
// resetting a timer is not required if it is going to be started again by
// nv_timer_begin()
static inline void
nv_timer_reset(timer* tmr)
{
  tmr->m_start = -1;
  tmr->m_end   = -1;
}

static inline bool
nv_timer_is_done(const timer* tmr)
{
  if (tmr->m_end == -1 || tmr->m_start == -1)
  {
    nv_log_warning("Timer end is invalid (it has been reset or was not created "
                   "correctly). The timer needs to be restarted using nv_timer_begin()");
    return 0;
  }

  return _nv_timer_get_currtime() >= tmr->m_end;
}

static inline NV_TIMER_TIME_TYPE
nv_timer_time_since_start(const timer* tmr)
{
  return (_nv_timer_get_currtime() - tmr->m_start);
}

static inline NV_TIMER_TIME_TYPE
nv_timer_time_since_done(const timer* tmr)
{
  return (_nv_timer_get_currtime() - tmr->m_end);
}

NOVA_HEADER_END

#endif //__TIMER_H__
