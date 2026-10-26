#include "decode_helpers.h"

////////////////////////////////////////////////////////////////////////////////
int bfmax_decoder(
   OUT DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT],
   IN  POS Htr_sparse[N0][PAD32(D)],
   IN  POS H_sparse[N0][PAD32(D)],
   IN  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT])
{
   /* expand each syndrome bit to u8 */
   /* expand each syndrome bit to u8 */
   uint8_t syndrome_bits[R_BITS];
   dense_to_u8(syndrome_bits, syndrome, R_BITS);
   /* weight from the R_BITS real bits only (ignores any garbage in the padding) */
   int hw = 0;
   for (int i = 0; i < R_BITS; i++) hw += syndrome_bits[i];
   DEBUG_PRINT("i: START \t hw(s): %d \n", hw);

   /* per-key tables (not thread-safe as static; better kept with the secret key) */
   static bfmax_ctx_t ctx;
   bfmax_ctx_init(&ctx, Htr_sparse, H_sparse);

   /* compute unsatisfied parity checks (padding bytes stay 0 and are never used) */
   ALIGNED uint8_t upc[PAD8(N0 * R_BITS)] = {0};
   compute_upcs(upc, Htr_sparse, syndrome_bits);
   uint8_t mx = bfmax_max(upc);

   /* decoding iterations */
   int iter = 0;
   while (iter < ITER_MAX_BFMAX && hw != 0) {
      POS flip = bfmax_find(upc, mx);
      gf2x_toggle_coeff(error + (flip / R_BITS) * NUM_DIGITS_GF2X_ELEMENT, flip % R_BITS);
      hw = bfmax_update(&ctx, upc, Htr_sparse, flip, syndrome_bits, hw, &mx);
      iter++;
      DEBUG_PRINT("i: %d \t flip: %u \t hw(s): %d \n", iter, flip, hw);
   }
   return hw == 0;
}
////////////////////////////////////////////////////////////////////////////////
void decode_bfmax_internal(OUT e_t *e, IN const ct_t *ct, IN const sk_t *sk)
{
  DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT] = {0};
  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT] = {0};
  ALIGNED POS Htr_sparse[N0][PAD32(D)] = {0};
  ALIGNED POS H_sparse[N0][PAD32(D)] = {0};

  pad_r_t h0 = {0};
  h0.val = sk->bin[0];

  pad_r_t c0 = {0};
  c0.val = ct->c0;

  for(int block=0; block<N0; block++) {
    for(int j=0; j<D; j++) {
      Htr_sparse[block][j] = sk->wlist[block].val[j];
      H_sparse[block][j] = (R_BITS - sk->wlist[block].val[j]) % R_BITS;
    }
  }

  /* pad_r_t is larger than NUM_DIGITS_GF2X_ELEMENT digits: multiply into a
   * pad_r_t, then copy only the R_BYTES real bytes (padding stays zero) */
  pad_r_t s = {0};
  gf2x_mod_mul(&s, &c0, &h0);
  memcpy(syndrome, s.val.raw, R_BYTES);

  bfmax_decoder(error, Htr_sparse, H_sparse, syndrome);

  for(int block=0; block<N0; block++) {
    memcpy(e->val[block].raw, error + block * NUM_DIGITS_GF2X_ELEMENT, R_BYTES);
  }
}
