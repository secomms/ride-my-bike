/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0"
 *
 * Written by Nir Drucker, Shay Gueron and Dusan Kostic,
 * AWS Cryptographic Algorithms Group.
 *
 * The inversion algorithm in this file is based on:
 * [1] Nir Drucker, Shay Gueron, and Dusan Kostic. 2020. "Fast polynomial
 * inversion for post quantum QC-MDPC cryptography". Cryptology ePrint Archive,
 * 2020. https://eprint.iacr.org/2020/298.pdf
 */

#include "cleanup.h"
#include "gf2x.h"
#include "gf2x_internal.h"
#include "gf2x_inv_params.h"

// a = a^2 mod (x^r - 1)
_INLINE_ void gf2x_mod_sqr_in_place(IN OUT pad_r_t *a,
                                    OUT dbl_pad_r_t *secure_buffer,
                                    IN const gf2x_ctx *ctx)
{
  ctx->sqr(secure_buffer, a);
  ctx->red(a, secure_buffer);
}

// c = a^2^2^num_sqrs
_INLINE_ void repeated_squaring(OUT pad_r_t *c,
                                IN pad_r_t *    a,
                                IN const size_t num_sqrs,
                                OUT dbl_pad_r_t *sec_buf,
                                IN const gf2x_ctx *ctx)
{
  c->val = a->val;

  for(size_t i = 0; i < num_sqrs; i++) {
    gf2x_mod_sqr_in_place(c, sec_buf, ctx);
  }
}


// k-squaring is computed by a permutation of bits of the input polynomial,
// as defined in [1](Observation 1). The required parameter for the permutation
// is l = (2^k)^-1 % R.
// Therefore, there are two sets of parameters for every exponentiation:
//   - exp0_k and exp1_k
//   - exp0_l and exp1_l

// Exponentiation 0 computes f^2^2^(i-1) for 0 < i < MAX_I.
// Exponentiation 1 computes f^2^((r-2) % 2^i) for 0 < i < MAX_I,
// only when the i-th bit of (r-2) is 1. Therefore, the value 0 in
// exp1_k[i] and exp1_l[i] means that exp1 is skipped in i-th iteration.



// Inversion in F_2[x]/(x^R - 1), [1](Algorithm 2).
// c = a^{-1} mod x^r-1
void gf2x_mod_inv(OUT pad_r_t *c, IN const pad_r_t *a)
{
  // Initialize gf2x methods struct
  gf2x_ctx ctx;
  gf2x_ctx_init(&ctx);

  // Note that exp0/1_k/l are predefined constants that depend only on the value
  // of R. This value is public. Therefore, branches in this function, which
  // depends on R, are also "public". Code that releases these branches
  // (taken/not-taken) does not leak secret information.
  const size_t exp0_k[MAX_I] = {EXP0_K_VALS};
  const size_t exp0_l[MAX_I] = {EXP0_L_VALS};
  const size_t exp1_k[MAX_I] = {EXP1_K_VALS};
  const size_t exp1_l[MAX_I] = {EXP1_L_VALS};

  DEFER_CLEANUP(pad_r_t f = {0}, pad_r_cleanup);
  DEFER_CLEANUP(pad_r_t g = {0}, pad_r_cleanup);
  DEFER_CLEANUP(pad_r_t t = {0}, pad_r_cleanup);
  DEFER_CLEANUP(dbl_pad_r_t sec_buf = {0}, dbl_pad_r_cleanup);

  // Steps 2 and 3 in [1](Algorithm 2)
  f.val = a->val;
  t.val = a->val;

  for(size_t i = 1; i < MAX_I; i++) {
    // Step 5 in [1](Algorithm 2), exponentiation 0: g = f^2^2^(i-1)
    if(exp0_k[i - 1] <= K_SQR_THR) {
      repeated_squaring(&g, &f, exp0_k[i - 1], &sec_buf, &ctx);
    } else {
      ctx.k_sqr(&g, &f, exp0_l[i - 1]);
    }

    // Step 6, [1](Algorithm 2): f = f*g
    gf2x_mod_mul_with_ctx(&f, &g, &f, &ctx);

    if(exp1_k[i] != 0) {
      // Step 8, [1](Algorithm 2), exponentiation 1: g = f^2^((r-2) % 2^i)
      if(exp1_k[i] <= K_SQR_THR) {
        repeated_squaring(&g, &f, exp1_k[i], &sec_buf, &ctx);
      } else {
        ctx.k_sqr(&g, &f, exp1_l[i]);
      }

      // Step 9, [1](Algorithm 2): t = t*g;
      gf2x_mod_mul_with_ctx(&t, &g, &t, &ctx);
    }
  }

  // Step 10, [1](Algorithm 2): c = t^2
  gf2x_mod_sqr_in_place(&t, &sec_buf, &ctx);
  c->val = t.val;
}
