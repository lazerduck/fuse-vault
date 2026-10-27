#include "internal.h"
#include <string.h>

/* Twofish: 128-bit block cipher. Designed by Schneier, Kelsey, Whiting,
 * Wagner, Hall and Ferguson (Counterpane, US). Unpatented, public domain.
 *
 * Clean-room C implementation of the algorithm as specified in the Twofish
 * paper, ported from the Go crypto/twofish reference implementation
 * (BSD-3-clause, Google), which is itself a port of LibTomCrypt (public
 * domain). Validated against the published Twofish known-answer test vectors.
 *
 * Only the 256-bit key variant is implemented, used as Twofish-256-XTS.
 */

/* --- Fixed S-boxes (q0 / q1 permutations, 4x16 nibbles each) --- */
static const uint8_t qbox[2][4][16] = {
    {{
        0x8, 0x1, 0x7, 0xD, 0x6, 0xF, 0x3, 0x2, 0x0, 0xB, 0x5, 0x9, 0xE, 0xC, 0xA, 0x4
    }, {
        0xE, 0xC, 0xB, 0x8, 0x1, 0x2, 0x3, 0x5, 0xF, 0x4, 0xA, 0x6, 0x7, 0x0, 0x9, 0xD
    }, {
        0xB, 0xA, 0x5, 0xE, 0x6, 0xD, 0x9, 0x0, 0xC, 0x8, 0xF, 0x3, 0x2, 0x4, 0x7, 0x1
    }, {
        0xD, 0x7, 0xF, 0x4, 0x1, 0x2, 0x6, 0xE, 0x9, 0xB, 0x3, 0x0, 0x8, 0x5, 0xC, 0xA
    }},
    {{
        0x2, 0x8, 0xB, 0xD, 0xF, 0x7, 0x6, 0xE, 0x3, 0x1, 0x9, 0x4, 0x0, 0xA, 0xC, 0x5
    }, {
        0x1, 0xE, 0x2, 0xB, 0x4, 0xC, 0x3, 0x7, 0x6, 0xD, 0xA, 0x5, 0xF, 0x9, 0x0, 0x8
    }, {
        0x4, 0xC, 0x7, 0x5, 0x1, 0x6, 0x9, 0xA, 0x0, 0xE, 0xD, 0x8, 0x2, 0xB, 0x3, 0xF
    }, {
        0xB, 0x9, 0x5, 0x1, 0xC, 0x3, 0xD, 0xE, 0x6, 0x4, 0x7, 0xF, 0x2, 0x0, 0x8, 0xA
    }},
};

static uint8_t sbox[2][256];
static int sbox_built = 0;

static uint8_t gen_q(int qi, uint8_t x) {
    uint8_t a0 = (uint8_t)(x / 16), b0 = (uint8_t)(x % 16);
    for (int i = 0; i < 2; i++) {
        uint8_t a1 = (uint8_t)(a0 ^ b0);
        uint8_t b1 = (uint8_t)((a0 ^ ((b0 << 3) | (b0 >> 1)) ^ (a0 << 3)) & 15);
        a0 = qbox[qi][2 * i][a1];
        b0 = qbox[qi][2 * i + 1][b1];
    }
    return (uint8_t)((b0 << 4) + a0);
}

static void build_sboxes(void) {
    if (sbox_built) return;
    for (int i = 0; i < 256; i++) {
        sbox[0][i] = gen_q(0, (uint8_t)i);
        sbox[1][i] = gen_q(1, (uint8_t)i);
    }
    sbox_built = 1;
}

/* --- GF(2^8) multiplication --- */
/* MDS polynomial: 0x169 = x^8 + x^6 + x^5 + x^3 + 1 */
static uint8_t gf_mult_mds(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    uint8_t aa = a, bb = b;
    for (int i = 0; i < 8; i++) {
        if (aa & 1) result ^= bb;
        aa >>= 1;
        uint8_t carry = bb & 0x80;
        bb <<= 1;
        if (carry) bb ^= 0x69; /* 0x169 & 0xFF */
    }
    return result;
}

/* RS polynomial: 0x14d = x^8 + x^6 + x^3 + x^2 + 1 */
static uint8_t gf_mult_rs(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    uint8_t aa = a, bb = b;
    for (int i = 0; i < 8; i++) {
        if (aa & 1) result ^= bb;
        aa >>= 1;
        uint8_t carry = bb & 0x80;
        bb <<= 1;
        if (carry) bb ^= 0x4d; /* 0x14d & 0xFF */
    }
    return result;
}

/* --- RS matrix (for key schedule S[] computation) --- */
static const uint8_t rs_matrix[4][8] = {
    {0x01, 0xA4, 0x55, 0x87, 0x5A, 0x58, 0xDB, 0x9E},
    {0xA4, 0x56, 0x82, 0xF3, 0x1E, 0xC6, 0x68, 0xE5},
    {0x02, 0xA1, 0xFC, 0xC1, 0x47, 0xAE, 0x3D, 0x19},
    {0xA4, 0x55, 0x87, 0x5A, 0x58, 0xDB, 0x9E, 0x03},
};

/* --- MDS matrix column multiplication --- */
static uint32_t mds_column_mult(uint8_t in, int col) {
    uint8_t mul01 = in;
    uint8_t mul5B = gf_mult_mds(in, 0x5B);
    uint8_t mulEF = gf_mult_mds(in, 0xEF);
    switch (col) {
    case 0:
        return (uint32_t)mul01 | ((uint32_t)mul5B << 8) |
               ((uint32_t)mulEF << 16) | ((uint32_t)mulEF << 24);
    case 1:
        return (uint32_t)mulEF | ((uint32_t)mulEF << 8) |
               ((uint32_t)mul5B << 16) | ((uint32_t)mul01 << 24);
    case 2:
        return (uint32_t)mul5B | ((uint32_t)mulEF << 8) |
               ((uint32_t)mul01 << 16) | ((uint32_t)mulEF << 24);
    case 3:
    default:
        return (uint32_t)mul5B | ((uint32_t)mul01 << 8) |
               ((uint32_t)mulEF << 16) | ((uint32_t)mul5B << 24);
    }
}

/* --- h function: key-dependent S-box generation (256-bit key, all 3 cases) --- */
static uint32_t h_func(const uint8_t in[4], const uint8_t *key, int offset) {
    uint8_t y[4];
    memcpy(y, in, 4);

    /* case 4 (256-bit): key[4*(6+offset) .. +3] */
    y[0] = (uint8_t)(sbox[1][y[0]] ^ key[4 * (6 + offset) + 0]);
    y[1] = (uint8_t)(sbox[0][y[1]] ^ key[4 * (6 + offset) + 1]);
    y[2] = (uint8_t)(sbox[0][y[2]] ^ key[4 * (6 + offset) + 2]);
    y[3] = (uint8_t)(sbox[1][y[3]] ^ key[4 * (6 + offset) + 3]);

    /* case 3 (192-bit): key[4*(4+offset) .. +3] */
    y[0] = (uint8_t)(sbox[1][y[0]] ^ key[4 * (4 + offset) + 0]);
    y[1] = (uint8_t)(sbox[1][y[1]] ^ key[4 * (4 + offset) + 1]);
    y[2] = (uint8_t)(sbox[0][y[2]] ^ key[4 * (4 + offset) + 2]);
    y[3] = (uint8_t)(sbox[0][y[3]] ^ key[4 * (4 + offset) + 3]);

    /* case 2 (128-bit): key[4*(0+offset) .. +3] and key[4*(2+offset) .. +3] */
    y[0] = sbox[1][sbox[0][sbox[0][y[0]] ^ key[4 * (0 + offset) + 0]] ^
                     key[4 * (2 + offset) + 0]];
    y[1] = sbox[0][sbox[0][sbox[1][y[1]] ^ key[4 * (0 + offset) + 1]] ^
                     key[4 * (2 + offset) + 1]];
    y[2] = sbox[1][sbox[1][sbox[0][y[2]] ^ key[4 * (0 + offset) + 2]] ^
                     key[4 * (2 + offset) + 2]];
    y[3] = sbox[0][sbox[1][sbox[1][y[3]] ^ key[4 * (0 + offset) + 3]] ^
                     key[4 * (2 + offset) + 3]];

    /* [y0 y1 y2 y3] = MDS . [x0 x1 x2 x3] */
    uint32_t mds_mult = 0;
    for (int i = 0; i < 4; i++) {
        mds_mult ^= mds_column_mult(y[i], i);
    }
    return mds_mult;
}

/* --- Twofish key context --- */
typedef fv_twofish_ctx twofish_ctx;

/* --- Key schedule (32-byte / 256-bit key only) --- */
static int twofish_key_schedule(twofish_ctx *ctx, const uint8_t *key) {
    build_sboxes();

    /* Compute S[] words: RS matrix applied to the 4 64-bit key words. */
    uint8_t S[16];
    memset(S, 0, 16);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int kk = 0; kk < 8; kk++) {
                S[4 * i + j] ^= gf_mult_rs(key[8 * i + kk], rs_matrix[j][kk]);
            }
        }
    }

    /* Calculate 40 subkeys: 8 whitening + 32 round. */
    uint8_t tmp[4];
    for (int i = 0; i < 20; i++) {
        tmp[0] = tmp[1] = tmp[2] = tmp[3] = (uint8_t)(2 * i);
        uint32_t A = h_func(tmp, key, 0);

        tmp[0] = tmp[1] = tmp[2] = tmp[3] = (uint8_t)(2 * i + 1);
        uint32_t B = h_func(tmp, key, 1);
        B = (B << 8) | (B >> 24); /* rotl32 by 8 */

        ctx->k[2 * i] = A + B;
        uint32_t tmp32 = 2 * B + A;
        ctx->k[2 * i + 1] = (tmp32 << 9) | (tmp32 >> (32 - 9)); /* rotl32 by 9 */
    }

    /* Calculate key-dependent S-boxes (256-bit case). */
    for (int i = 0; i < 256; i++) {
        uint8_t b = (uint8_t)i;
        ctx->sbox[0][i] = mds_column_mult(
            (uint8_t)(sbox[1][sbox[0][sbox[0][sbox[1][sbox[1][b] ^ S[0]] ^ S[4]] ^ S[8]] ^ S[12]]),
            0);
        ctx->sbox[1][i] = mds_column_mult(
            (uint8_t)(sbox[0][sbox[0][sbox[1][sbox[1][sbox[0][b] ^ S[1]] ^ S[5]] ^ S[9]] ^ S[13]]),
            1);
        ctx->sbox[2][i] = mds_column_mult(
            (uint8_t)(sbox[1][sbox[1][sbox[0][sbox[0][sbox[0][b] ^ S[2]] ^ S[6]] ^ S[10]] ^ S[14]]),
            2);
        ctx->sbox[3][i] = mds_column_mult(
            (uint8_t)(sbox[0][sbox[1][sbox[1][sbox[0][sbox[1][b] ^ S[3]] ^ S[7]] ^ S[11]] ^ S[15]]),
            3);
    }

    return 0;
}

/* --- Encrypt one 16-byte block --- */
static void twofish_encrypt_block(const twofish_ctx *ctx, const uint8_t in[16],
                                  uint8_t out[16]) {
    const uint32_t *S1 = ctx->sbox[0];
    const uint32_t *S2 = ctx->sbox[1];
    const uint32_t *S3 = ctx->sbox[2];
    const uint32_t *S4 = ctx->sbox[3];
    const uint32_t *k = ctx->k;

    uint32_t ia, ib, ic, id;
    ia = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    ib = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    ic = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    id = (uint32_t)in[12] | ((uint32_t)in[13] << 8) | ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);

    /* Pre-whitening. */
    ia ^= k[0];
    ib ^= k[1];
    ic ^= k[2];
    id ^= k[3];

    for (int i = 0; i < 8; i++) {
        const uint32_t *rk = &k[8 + i * 4];
        uint32_t t2 = S2[(uint8_t)ib] ^ S3[(uint8_t)(ib >> 8)] ^
                      S4[(uint8_t)(ib >> 16)] ^ S1[(uint8_t)(ib >> 24)];
        uint32_t t1 = (S1[(uint8_t)ia] ^ S2[(uint8_t)(ia >> 8)] ^
                       S3[(uint8_t)(ia >> 16)] ^ S4[(uint8_t)(ia >> 24)]) + t2;
        uint32_t x = ic ^ (t1 + rk[0]);
        ic = (x >> 1) | (x << 31); /* rotr32 by 1 */
        id = ((id << 1) | (id >> 31)) ^ (t2 + t1 + rk[1]); /* rotl32 by 1 */

        t2 = S2[(uint8_t)id] ^ S3[(uint8_t)(id >> 8)] ^
             S4[(uint8_t)(id >> 16)] ^ S1[(uint8_t)(id >> 24)];
        t1 = (S1[(uint8_t)ic] ^ S2[(uint8_t)(ic >> 8)] ^
              S3[(uint8_t)(ic >> 16)] ^ S4[(uint8_t)(ic >> 24)]) + t2;
        x = ia ^ (t1 + rk[2]);
        ia = (x >> 1) | (x << 31);
        ib = ((ib << 1) | (ib >> 31)) ^ (t2 + t1 + rk[3]);
    }

    /* Output with "undo last swap". */
    uint32_t ta = ic ^ k[4];
    uint32_t tb = id ^ k[5];
    uint32_t tc = ia ^ k[6];
    uint32_t td = ib ^ k[7];

    out[0] = (uint8_t)ta;
    out[1] = (uint8_t)(ta >> 8);
    out[2] = (uint8_t)(ta >> 16);
    out[3] = (uint8_t)(ta >> 24);
    out[4] = (uint8_t)tb;
    out[5] = (uint8_t)(tb >> 8);
    out[6] = (uint8_t)(tb >> 16);
    out[7] = (uint8_t)(tb >> 24);
    out[8] = (uint8_t)tc;
    out[9] = (uint8_t)(tc >> 8);
    out[10] = (uint8_t)(tc >> 16);
    out[11] = (uint8_t)(tc >> 24);
    out[12] = (uint8_t)td;
    out[13] = (uint8_t)(td >> 8);
    out[14] = (uint8_t)(td >> 16);
    out[15] = (uint8_t)(td >> 24);
}

/* --- Decrypt one 16-byte block --- */
static void twofish_decrypt_block(const twofish_ctx *ctx, const uint8_t in[16],
                                  uint8_t out[16]) {
    const uint32_t *S1 = ctx->sbox[0];
    const uint32_t *S2 = ctx->sbox[1];
    const uint32_t *S3 = ctx->sbox[2];
    const uint32_t *S4 = ctx->sbox[3];
    const uint32_t *k = ctx->k;

    uint32_t ta, tb, tc, td;
    ta = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    tb = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    tc = (uint32_t)in[8] | ((uint32_t)in[9] << 8) | ((uint32_t)in[10] << 16) | ((uint32_t)in[11] << 24);
    td = (uint32_t)in[12] | ((uint32_t)in[13] << 8) | ((uint32_t)in[14] << 16) | ((uint32_t)in[15] << 24);

    /* Undo "undo final swap". */
    uint32_t ia = tc ^ k[6];
    uint32_t ib = td ^ k[7];
    uint32_t ic = ta ^ k[4];
    uint32_t id = tb ^ k[5];

    for (int i = 8; i > 0; i--) {
        const uint32_t *rk = &k[4 + i * 4];
        uint32_t t2 = S2[(uint8_t)id] ^ S3[(uint8_t)(id >> 8)] ^
                      S4[(uint8_t)(id >> 16)] ^ S1[(uint8_t)(id >> 24)];
        uint32_t t1 = (S1[(uint8_t)ic] ^ S2[(uint8_t)(ic >> 8)] ^
                       S3[(uint8_t)(ic >> 16)] ^ S4[(uint8_t)(ic >> 24)]) + t2;
        ia = ((ia << 1) | (ia >> 31)) ^ (t1 + rk[2]); /* rotl32 by 1 */
        uint32_t y = ib ^ (t2 + t1 + rk[3]);
        ib = (y >> 1) | (y << 31); /* rotr32 by 1 */

        t2 = S2[(uint8_t)ib] ^ S3[(uint8_t)(ib >> 8)] ^
             S4[(uint8_t)(ib >> 16)] ^ S1[(uint8_t)(ib >> 24)];
        t1 = (S1[(uint8_t)ia] ^ S2[(uint8_t)(ia >> 8)] ^
              S3[(uint8_t)(ia >> 16)] ^ S4[(uint8_t)(ia >> 24)]) + t2;
        ic = ((ic << 1) | (ic >> 31)) ^ (t1 + rk[0]);
        y = id ^ (t2 + t1 + rk[1]);
        id = (y >> 1) | (y << 31);
    }

    /* Undo pre-whitening. */
    ia ^= k[0];
    ib ^= k[1];
    ic ^= k[2];
    id ^= k[3];

    out[0] = (uint8_t)ia;
    out[1] = (uint8_t)(ia >> 8);
    out[2] = (uint8_t)(ia >> 16);
    out[3] = (uint8_t)(ia >> 24);
    out[4] = (uint8_t)ib;
    out[5] = (uint8_t)(ib >> 8);
    out[6] = (uint8_t)(ib >> 16);
    out[7] = (uint8_t)(ib >> 24);
    out[8] = (uint8_t)ic;
    out[9] = (uint8_t)(ic >> 8);
    out[10] = (uint8_t)(ic >> 16);
    out[11] = (uint8_t)(ic >> 24);
    out[12] = (uint8_t)id;
    out[13] = (uint8_t)(id >> 8);
    out[14] = (uint8_t)(id >> 16);
    out[15] = (uint8_t)(id >> 24);
}

/* --- Backend ops --- */

static int twofish_init(fv_key_context *c, const uint8_t *k) {
    /* k points to 64 bytes: data key first 32, tweak key last 32.
     * twofish[0]=enc, [1]=dec, [2]=tweak. */
    if (twofish_key_schedule(&c->twofish[0], k)) return -1;
    c->twofish[1] = c->twofish[0]; /* dec: same S-boxes/subkeys, accessed reversed */
    if (twofish_key_schedule(&c->twofish[2], k + 32)) return -1;
    return 0;
}

static int twofish_block(fv_key_context *c, int enc, int tweak,
                         const uint8_t *in, uint8_t *out) {
    if (tweak) {
        twofish_encrypt_block(&c->twofish[2], in, out);
    } else if (enc) {
        twofish_encrypt_block(&c->twofish[0], in, out);
    } else {
        twofish_decrypt_block(&c->twofish[1], in, out);
    }
    return 0;
}

const fv_cipher_ops fv_twofish_ops = {"Twofish-256-XTS", twofish_init, twofish_block};
