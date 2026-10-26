#include "decode.h"
#include "cleanup.h"
#include "decode_internal.h"
#include "gf2x.h"
#include "utilities.h"
#include "decode_helpers.h"

void compute_syndrome(OUT syndrome_t *syndrome,
                      IN const pad_r_t *c0,
                      IN const pad_r_t *h0,
                      IN const decode_ctx *ctx)
{
  DEFER_CLEANUP(pad_r_t pad_s, pad_r_cleanup);

  gf2x_mod_mul(&pad_s, c0, h0);

  bike_memcpy((uint8_t *)syndrome->qw, pad_s.val.raw, R_BYTES);
  ctx->dup(syndrome);
}

_INLINE_ void recompute_syndrome(OUT syndrome_t *syndrome,
                                 IN const pad_r_t *c0,
                                 IN const pad_r_t *h0,
                                 IN const pad_r_t *pk,
                                 IN const e_t *e,
                                 IN const decode_ctx *ctx)
{
  DEFER_CLEANUP(pad_r_t tmp_c0, pad_r_cleanup);
  DEFER_CLEANUP(pad_r_t e0 = {0}, pad_r_cleanup);
  DEFER_CLEANUP(pad_r_t e1 = {0}, pad_r_cleanup);

  e0.val = e->val[0];
  e1.val = e->val[1];

  // tmp_c0 = pk * e1 + c0 + e0
  gf2x_mod_mul(&tmp_c0, &e1, pk);
  gf2x_mod_add(&tmp_c0, &tmp_c0, c0);
  gf2x_mod_add(&tmp_c0, &tmp_c0, &e0);

  // Recompute the syndrome using the updated ciphertext
  compute_syndrome(syndrome, &tmp_c0, h0, ctx);
}

// Calculate the Unsatisfied Parity Checks (UPCs) and update the errors
// vector (e) accordingly.
_INLINE_ void find_err(OUT e_t *e,
                        IN const syndrome_t *          syndrome,
                        IN const compressed_idx_d_ar_t wlist,
                        IN const uint8_t               threshold,
                        IN const decode_ctx *ctx)
{
  // This function uses the bit-slice-adder methodology of [5]:
  DEFER_CLEANUP(syndrome_t rotated_syndrome = {0}, syndrome_cleanup);
  DEFER_CLEANUP(upc_t upc, upc_cleanup);

  for(uint32_t i = 0; i < N0; i++) {
    // UPC must start from zero at every iteration
    bike_memset(&upc, 0, sizeof(upc));

    // 1) Right-rotate the syndrome for every secret key set bit index
    //    Then slice-add it to the UPC array.
    for(size_t j = 0; j < D; j++) {
      ctx->rotate_right(&rotated_syndrome, syndrome, wlist[i].val[j]);
      ctx->bit_sliced_adder(&upc, &rotated_syndrome, LOG2_MSB(j + 1));
    }

    // 2) Subtract the threshold from the UPC counters
    ctx->bit_slice_full_subtract(&upc, threshold);

    // 3) Update the errors and the black errors vectors.
    //    The last slice of the UPC array holds the MSB of the accumulated values
    //    minus the threshold. Every zero bit indicates a potential error bit.
    //    The errors values are stored in the black array and xored with the
    //    errors Of the previous iteration.
    const r_t *last_slice = &(upc.slice[SLICES - 1].u.r.val);
    for(size_t j = 0; j < R_BYTES; j++) {
      const uint8_t sum_msb  = (~last_slice->raw[j]);
      e->val[i].raw[j] ^= sum_msb;
    }

    // Ensure that the padding bits (upper bits of the last byte) are zero so
    // they will not be included in the multiplication and in the hash function.
    e->val[i].raw[R_BYTES - 1] &= LAST_R_BYTE_MASK;
  }
}

void decode_oop_inner(OUT e_t *e, IN syndrome_t *s, IN const sk_t *sk, IN const pad_r_t  *c0, IN const pad_r_t *h0, IN const pad_r_t *pk, IN const decode_ctx * ctx)
{
   uint8_t thresholds[3] = {TH0,TH1,TH2};

   for(uint32_t iter = 0; iter < ITER_MAX_BF; iter++) {
     find_err(e, s, sk->wlist, iter < sizeof(thresholds)/sizeof(uint8_t) ? thresholds[iter] : (D+1)/2, ctx);
     recompute_syndrome(s, c0, h0, pk, e, ctx);
     if(r_bits_vector_weight((r_t*)s->qw) == 0) break;
   }
}

#include <stdio.h>
void decode_oop_internal(OUT e_t *e, IN const ct_t *ct, IN const sk_t *sk)
{
   decode_ctx ctx;
   decode_ctx_init(&ctx);

   DEFER_CLEANUP(pad_r_t c0 = {0}, pad_r_cleanup);
   DEFER_CLEANUP(pad_r_t h0 = {0}, pad_r_cleanup);
   pad_r_t pk = {0};
 
   // Pad ciphertext (c0), secret key (h0), and public key (h)
   c0.val = ct->c0;
   h0.val = sk->bin[0];
   pk.val = sk->pk;
   
   DEFER_CLEANUP(syndrome_t s = {0}, syndrome_cleanup);
   DMSG("  Computing s.\n");
   compute_syndrome(&s, &c0, &h0, &ctx);

   ctx.dup(&s);
   
   bike_memset(e, 0, sizeof(*e));

   decode_oop_inner(e,&s,sk,&c0,&h0,&pk,&ctx);

}
