/* nist-keywrap.c

   AES Key Wrap function.
   implements RFC 3394
   https://tools.ietf.org/html/rfc3394

   Copyright (C) 2021 Nicolas Mora
                 2021 Niels Möller

   This file is part of GNU Nettle.

   GNU Nettle is free software: you can redistribute it and/or
   modify it under the terms of either:

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at your
       option) any later version.

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at your
       option) any later version.

   or both in parallel, as here.

   GNU Nettle is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see http://www.gnu.org/licenses/.
*/

/* Fuse Vault adaptation: generic KW functions only; local compatibility types
 * and namespace. The upstream wrap/unwrap loops are unchanged. Scratch values
 * are explicitly erased before return. See README.md for provenance. */
#include "keywrap_compat.h"

void
nist_keywrap16 (const void *ctx, nettle_cipher_func *encrypt,
		const uint8_t *iv, size_t ciphertext_length,
		uint8_t *ciphertext, const uint8_t *cleartext)
{
  union nettle_block16 I, B;
  union nettle_block8 A;
  size_t i, j, n;
  uint8_t *R = ciphertext + 8;

  /* ciphertext_length must be at least 16
   * and be divisible by 8 */
  assert (ciphertext_length >= 16);
  assert (!(ciphertext_length % 8));

  n = (ciphertext_length - 8) / 8;
  memcpy (R, cleartext, (ciphertext_length - 8));
  memcpy (A.b, iv, 8);

  for (j = 0; j < 6; j++)
    {
      for (i = 0; i < n; i++)
	{
	  /* I = A | R[1] */
	  I.u64[0] = A.u64;
	  memcpy (I.b + 8, R + (i * 8), 8);

	  /* B = AES(K, I) */
	  encrypt (ctx, 16, B.b, I.b);

	  /* A = MSB(64, B) ^ t where t = (n*j)+i */
	  A.u64 = B.u64[0] ^ bswap64_if_le ((n * j) + (i + 1));

	  /* R[i] = LSB(64, B) */
	  memcpy (R + (i * 8), B.b + 8, 8);
	}
    }

  memcpy (ciphertext, A.b, 8);
  fv_kw_zero(&I, sizeof(I)); fv_kw_zero(&B, sizeof(B)); fv_kw_zero(&A, sizeof(A));
}

int
nist_keyunwrap16 (const void *ctx, nettle_cipher_func *decrypt,
		  const uint8_t *iv, size_t cleartext_length,
		  uint8_t *cleartext, const uint8_t *ciphertext)
{
  union nettle_block16 I, B;
  union nettle_block8 A;
  int i, j;
  size_t n;
  uint8_t *R = cleartext;

  /* cleartext_length must be at least 8
   * and be divisible by 8 */
  assert (cleartext_length >= 8);
  assert (!(cleartext_length % 8));

  n = (cleartext_length / 8);
  memcpy (A.b, ciphertext, 8);
  memcpy (R, ciphertext + 8, cleartext_length);

  for (j = 5; j >= 0; j--)
    {
      for (i = n - 1; i >= 0; i--)
	{
	  /* B = AES-1(K, (A ^ t) | R[i]) where t = n*j+i */
	  I.u64[0] = A.u64 ^ bswap64_if_le ((n * j) + (i + 1));
	  memcpy (I.b + 8, R + (i * 8), 8);
	  decrypt (ctx, 16, B.b, I.b);

	  /* A = MSB(64, B) */
	  A.u64 = B.u64[0];

	  /* R[i] = LSB(64, B) */
	  memcpy (R + (i * 8), B.b + 8, 8);
	}
    }

  int valid = memeql_sec (A.b, iv, 8);
  fv_kw_zero(&I, sizeof(I)); fv_kw_zero(&B, sizeof(B)); fv_kw_zero(&A, sizeof(A));
  return valid;
}
