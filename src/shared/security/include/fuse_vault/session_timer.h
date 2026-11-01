#ifndef FV_SESSION_TIMER_H
#define FV_SESSION_TIMER_H
#include <stdbool.h>
#include <stdint.h>

/* Owned by the vault worker. Callers supply the device's monotonic clock in
 * milliseconds; host timestamps must never control session lifetime. */
typedef struct {
    uint64_t deadline_ms;
    uint32_t timeout_seconds;
} fv_session_timer;

/* Zero seconds disables the timer. Starting is for a newly authenticated
 * session, not for ordinary activity: only start can revive an expired timer. */
void fv_session_timer_start(fv_session_timer *, uint32_t timeout_seconds, uint64_t now_ms);
void fv_session_timer_stop(fv_session_timer *);
/* Renew an active timer before its deadline. Returns false when off or expired;
 * activity at the deadline cannot rescue an already expired session. */
bool fv_session_timer_touch(fv_session_timer *, uint64_t now_ms);
bool fv_session_timer_expired(const fv_session_timer *, uint64_t now_ms);
/* Seconds rounded up for display, or zero when disabled/expired. */
uint32_t fv_session_timer_remaining(const fv_session_timer *, uint64_t now_ms);
#endif
