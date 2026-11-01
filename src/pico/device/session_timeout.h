#ifndef FV_SESSION_TIMEOUT_H
#define FV_SESSION_TIMEOUT_H
#include <stdbool.h>
#include <stdint.h>

/* Start/stop/poll/activity belong to the vault worker. All times come from the
 * device monotonic clock, in milliseconds. A session generation binds local
 * activity to the authorization lifetime in which the button was pressed. */
void fv_session_timeout_start(uint32_t seconds, uint64_t now_ms, uint32_t generation);
void fv_session_timeout_stop(void);
/* Both return true if this call expired and locked the session. Polling alone
 * never renews a deadline; activity renews only a live, matching session. */
bool fv_session_timeout_poll(uint64_t now_ms, uint32_t generation);
bool fv_session_timeout_activity(uint64_t now_ms, uint32_t generation);
/* Core 0 publishes local button activity and reads the display snapshot. */
void fv_session_timeout_local_activity(uint32_t generation);
uint32_t fv_session_timeout_remaining(void);
/* Application callback on the worker: revoke access, wipe keys, dismiss UI. */
void fv_session_timeout_lock(void);
#endif
