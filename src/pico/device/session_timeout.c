#include "session_timeout.h"
#include "fuse_vault/session_timer.h"
#include <stdatomic.h>

static fv_session_timer timer;
static uint32_t timer_generation, consumed_sequence;
static atomic_uint remaining_seconds;
/* Core 0 is the sole writer. An even sequence publishes one complete event;
 * the worker never spins or reads a generation from a half-written event. */
static atomic_uint local_sequence, local_generation;

void fv_session_timeout_start(uint32_t seconds, uint64_t now_ms, uint32_t generation) {
    timer_generation = generation;
    consumed_sequence = atomic_load(&local_sequence);
    fv_session_timer_start(&timer, seconds, now_ms);
    atomic_store(&remaining_seconds, fv_session_timer_remaining(&timer, now_ms));
}

void fv_session_timeout_stop(void) {
    fv_session_timer_stop(&timer);
    timer_generation = 0;
    consumed_sequence = atomic_load(&local_sequence);
    atomic_store(&remaining_seconds, 0);
}

void fv_session_timeout_local_activity(uint32_t generation) {
    atomic_fetch_add(&local_sequence, 1);
    atomic_store(&local_generation, generation);
    atomic_fetch_add(&local_sequence, 1);
}

static bool take_local_activity(uint32_t generation) {
    uint32_t sequence = atomic_load(&local_sequence);
    if ((sequence & 1u) || sequence == consumed_sequence) return false;
    uint32_t event_generation = atomic_load(&local_generation);
    if (sequence != atomic_load(&local_sequence)) return false;
    consumed_sequence = sequence;
    return event_generation == generation;
}

bool fv_session_timeout_poll(uint64_t now_ms, uint32_t generation) {
    if (generation != timer_generation) {
        fv_session_timeout_stop();
        return false;
    }
    /* Expiry precedes pending activity. An event arriving after a deadline
     * cannot keep a session alive merely because the worker was occupied. */
    if (fv_session_timer_expired(&timer, now_ms)) {
        fv_session_timeout_stop();
        fv_session_timeout_lock();
        return true;
    }
    if (take_local_activity(generation)) (void)fv_session_timer_touch(&timer, now_ms);
    atomic_store(&remaining_seconds, fv_session_timer_remaining(&timer, now_ms));
    return false;
}

bool fv_session_timeout_activity(uint64_t now_ms, uint32_t generation) {
    if (fv_session_timeout_poll(now_ms, generation)) return true;
    (void)fv_session_timer_touch(&timer, now_ms);
    atomic_store(&remaining_seconds, fv_session_timer_remaining(&timer, now_ms));
    return false;
}

uint32_t fv_session_timeout_remaining(void) {
    return atomic_load(&remaining_seconds);
}
