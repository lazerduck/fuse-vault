#include "fuse_vault/secret_input.h"

#include <stddef.h>

bool fv_secret_input_encode(const fv_app_t *app, fv_secret_encoding_t *encoding) {
    if (app == NULL || encoding == NULL) {
        return false;
    }

    *encoding = (fv_secret_encoding_t) {{
        0x46u, /* F */
        0x56u, /* V */
        0x01u, /* encoding format version */
        (uint8_t)FV_SECRET_METHOD_WHEELS_V1,
        FV_SECRET_WHEEL_COUNT,
        app->secret_wheels[0],
        app->secret_wheels[1],
        app->secret_wheels[2],
    }};
    return true;
}
