#include "fuse_vault/session_timer.h"

static uint64_t deadline(uint32_t seconds, uint64_t now_ms) {
    uint64_t duration_ms = (uint64_t)seconds * 1000;
    return duration_ms > UINT64_MAX - now_ms ? UINT64_MAX : now_ms + duration_ms;
}

void fv_session_timer_start(fv_session_timer *timer, uint32_t seconds, uint64_t now_ms) {
    timer->timeout_seconds = seconds;
    timer->deadline_ms = seconds ? deadline(seconds, now_ms) : 0;
}

void fv_session_timer_stop(fv_session_timer *timer) {
    timer->timeout_seconds = 0;
    timer->deadline_ms = 0;
}

bool fv_session_timer_expired(const fv_session_timer *timer, uint64_t now_ms) {
    return timer->timeout_seconds && now_ms >= timer->deadline_ms;
}

bool fv_session_timer_touch(fv_session_timer *timer, uint64_t now_ms) {
    if (!timer->timeout_seconds || fv_session_timer_expired(timer, now_ms)) return false;
    uint64_t next = deadline(timer->timeout_seconds, now_ms);
    /* Ignore delayed activity timestamps: they must not shorten the remaining
     * interval already established by newer activity. */
    if (next > timer->deadline_ms) timer->deadline_ms = next;
    return true;
}

uint32_t fv_session_timer_remaining(const fv_session_timer *timer, uint64_t now_ms) {
    if (!timer->timeout_seconds || fv_session_timer_expired(timer, now_ms)) return 0;
    uint64_t milliseconds = timer->deadline_ms - now_ms;
    uint64_t seconds = milliseconds / 1000 + (milliseconds % 1000 != 0);
    /* Defensive clamp for a clock sampled before start/activity. */
    return seconds > timer->timeout_seconds ? timer->timeout_seconds : (uint32_t)seconds;
}
