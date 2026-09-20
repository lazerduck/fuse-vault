#ifndef FV_CREDENTIAL_ENTRY_H
#define FV_CREDENTIAL_ENTRY_H
#include <stdint.h>
/* Profiles 3 and 4 each encode exactly four bytes. Profile is separately bound
 * into the envelope/KDF, so wheel values and word IDs are distinct credentials. */
typedef struct {
    uint16_t profile;
    uint8_t values[4],selected,count,depth,prefix;
} fv_credential_entry;
void fv_entry_begin(fv_credential_entry *,uint16_t profile);
/* key uses UI_UP..UI_BACK logical directions. 1=submit, -1=cancel, 0=editing. */
int fv_entry_key(fv_credential_entry *,unsigned key);
const char *fv_entry_word(unsigned index);
#endif
