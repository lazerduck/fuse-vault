#include "fuse_vault/secret_input.h"

#include <stddef.h>

static bool encode_wheels(const uint8_t wheels[FV_SECRET_WHEEL_COUNT],
                          fv_secret_encoding_t *encoding) {
    if (wheels == NULL || encoding == NULL) {
        return false;
    }

    *encoding = (fv_secret_encoding_t) {{
        0x46u, /* F */
        0x56u, /* V */
        0x01u, /* encoding format version */
        (uint8_t)FV_SECRET_METHOD_WHEELS_V1,
        FV_SECRET_WHEEL_COUNT,
        wheels[0],
        wheels[1],
        wheels[2],
    }};
    return true;
}

bool fv_secret_input_encode(const fv_app_t *app, fv_secret_encoding_t *encoding) {
    return app != NULL && encode_wheels(app->secret_wheels, encoding);
}

bool fv_setup_secret_encode(const fv_app_t *app, fv_secret_encoding_t *encoding) {
    return app != NULL && encode_wheels(app->setup_secret_wheels, encoding);
}
