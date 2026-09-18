#ifndef FV_DEVELOPMENT_POLICY_H
#define FV_DEVELOPMENT_POLICY_H
/* Accepted measured setting: approximately 1.40 seconds on the 150 MHz board.
 * Diagnostic KDF timing commands may explore other counts without enrolling. */
#define FV_ENROLLMENT_ITERATIONS 60000u
#define FV_ENROLLMENT_KDF_LIMITS ((fv_kdf_limits){60000u,60000u})
#endif
