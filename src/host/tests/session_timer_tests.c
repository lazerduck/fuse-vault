#include "fuse_vault/session_timer.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1);}} while (0)

int main(void) {
    fv_session_timer timer = {0};
    CHECK(!fv_session_timer_expired(&timer, UINT64_MAX));
    CHECK(!fv_session_timer_touch(&timer, 100));
    CHECK(!fv_session_timer_remaining(&timer, 100));

    fv_session_timer_start(&timer, 60, 1000);
    CHECK(fv_session_timer_remaining(&timer, 1000) == 60);
    CHECK(fv_session_timer_remaining(&timer, 1001) == 60);
    CHECK(fv_session_timer_remaining(&timer, 2000) == 59);
    CHECK(fv_session_timer_remaining(&timer, 60999) == 1);
    CHECK(!fv_session_timer_expired(&timer, 60999));
    CHECK(fv_session_timer_expired(&timer, 61000));
    CHECK(!fv_session_timer_remaining(&timer, 61000));
    CHECK(!fv_session_timer_touch(&timer, 61000));
    CHECK(!fv_session_timer_touch(&timer, 90000));
    CHECK(timer.deadline_ms == 61000);

    /* Genuine activity one millisecond before expiry renews the full interval;
     * older queued activity cannot move the new deadline backwards. */
    fv_session_timer_start(&timer, 60, 1000);
    CHECK(fv_session_timer_touch(&timer, 60999));
    CHECK(fv_session_timer_remaining(&timer, 60999) == 60);
    CHECK(fv_session_timer_touch(&timer, 2000));
    CHECK(timer.deadline_ms == 120999);
    CHECK(!fv_session_timer_expired(&timer, 120998));
    CHECK(fv_session_timer_expired(&timer, 120999));

    /* Explicit restart is a fresh authorization lifetime. */
    fv_session_timer_start(&timer, 300, 200000);
    CHECK(fv_session_timer_remaining(&timer, 200000) == 300);
    fv_session_timer_stop(&timer);
    CHECK(!timer.timeout_seconds && !timer.deadline_ms);
    CHECK(!fv_session_timer_touch(&timer, 200001));
    CHECK(!fv_session_timer_expired(&timer, UINT64_MAX));
    fv_session_timer_start(&timer, 0, UINT64_MAX);
    CHECK(!timer.timeout_seconds && !timer.deadline_ms);

    /* Cross the 32-bit millisecond rollover with no premature expiry. */
    uint64_t long_uptime = (uint64_t)UINT32_MAX - 100;
    fv_session_timer_start(&timer, 1800, long_uptime);
    CHECK(fv_session_timer_touch(&timer, long_uptime + 1000));
    CHECK(fv_session_timer_remaining(&timer, long_uptime + 1000) == 1800);
    CHECK(!fv_session_timer_expired(&timer, long_uptime + 1800999));
    CHECK(fv_session_timer_expired(&timer, long_uptime + 1801000));

    /* Full uint32 durations and uint64 uptime remain defined, without wrap. */
    fv_session_timer_start(&timer, UINT32_MAX, 0);
    CHECK(fv_session_timer_remaining(&timer, 0) == UINT32_MAX);
    CHECK(fv_session_timer_remaining(&timer, 999) == UINT32_MAX);
    CHECK(fv_session_timer_expired(&timer, (uint64_t)UINT32_MAX * 1000));
    fv_session_timer_start(&timer, 60, UINT64_MAX - 1500);
    CHECK(timer.deadline_ms == UINT64_MAX);
    CHECK(fv_session_timer_remaining(&timer, UINT64_MAX - 1500) == 2);
    CHECK(fv_session_timer_remaining(&timer, UINT64_MAX - 1) == 1);
    CHECK(!fv_session_timer_touch(&timer, UINT64_MAX));
    CHECK(fv_session_timer_expired(&timer, UINT64_MAX));
    puts("session inactivity deadline checks passed");
    return 0;
}
