#include "session_timeout.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if (!(x)) {fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1);}} while (0)

static unsigned locks;
void fv_session_timeout_lock(void) {
    ++locks;
    /* The application's central lock path can safely stop again. */
    fv_session_timeout_stop();
}

int main(void) {
    fv_session_timeout_start(60, 1000, 1);
    CHECK(fv_session_timeout_remaining() == 60);
    for (uint64_t now = 2000; now < 61000; now += 1000)
        CHECK(!fv_session_timeout_poll(now, 1));
    CHECK(fv_session_timeout_remaining() == 1);
    CHECK(fv_session_timeout_poll(61000, 1));
    CHECK(locks == 1 && !fv_session_timeout_remaining());
    CHECK(!fv_session_timeout_poll(61001, 1));
    CHECK(!fv_session_timeout_activity(61002, 1));
    CHECK(locks == 1 && !fv_session_timeout_remaining());

    fv_session_timeout_start(60, 0, 2);
    CHECK(!fv_session_timeout_activity(59999, 2));
    CHECK(fv_session_timeout_remaining() == 60);
    CHECK(!fv_session_timeout_poll(119998, 2));
    CHECK(fv_session_timeout_activity(119999, 2));
    CHECK(locks == 2 && !fv_session_timeout_remaining());

    /* Genuine button activity renews, while stale-session events are ignored. */
    fv_session_timeout_start(60, 0, 3);
    fv_session_timeout_local_activity(2);
    CHECK(!fv_session_timeout_poll(30000, 3));
    CHECK(fv_session_timeout_remaining() == 30);
    fv_session_timeout_local_activity(3);
    CHECK(!fv_session_timeout_poll(59000, 3));
    CHECK(fv_session_timeout_remaining() == 60);
    CHECK(!fv_session_timeout_poll(60000, 3));
    CHECK(fv_session_timeout_remaining() == 59); /* Event consumed once. */
    fv_session_timeout_local_activity(3);
    CHECK(fv_session_timeout_poll(119000, 3)); /* Deadline always wins. */
    CHECK(locks == 3 && !fv_session_timeout_remaining());

    /* Lock/new unlock discards queued events from the previous lifetime. */
    fv_session_timeout_local_activity(3);
    fv_session_timeout_start(60, 120000, 4);
    CHECK(!fv_session_timeout_poll(150000, 4));
    CHECK(fv_session_timeout_remaining() == 30);
    fv_session_timeout_local_activity(3);
    CHECK(!fv_session_timeout_poll(170000, 4));
    CHECK(fv_session_timeout_remaining() == 10);
    CHECK(fv_session_timeout_poll(180000, 4));
    CHECK(locks == 4);

    fv_session_timeout_start(60, 0, 5);
    CHECK(!fv_session_timeout_poll(1000, 6)); /* A replaced session owns no old timer. */
    CHECK(!fv_session_timeout_remaining());
    CHECK(!fv_session_timeout_poll(60000, 6));
    CHECK(locks == 4);

    fv_session_timeout_start(0, 0, 7);
    fv_session_timeout_local_activity(7);
    CHECK(!fv_session_timeout_poll(UINT64_MAX, 7));
    CHECK(!fv_session_timeout_activity(UINT64_MAX, 7));
    CHECK(!fv_session_timeout_remaining() && locks == 4);

    /* All uint32 generations, including rollover zero, are valid event tags. */
    fv_session_timeout_start(60, 0, 0);
    fv_session_timeout_local_activity(0);
    CHECK(!fv_session_timeout_poll(1000, 0));
    CHECK(fv_session_timeout_remaining() == 60);
    CHECK(fv_session_timeout_poll(61000, 0));
    CHECK(locks == 5);
    puts("worker session timeout and scoped activity checks passed");
    return 0;
}
