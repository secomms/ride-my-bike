#include "decode_helpers.h"

////////////////////////////////////////////////////////////////////////////////
int bfmax_decoder(
   OUT DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT], 
   IN  POS Htr_sparse[N0][PAD32(D)], 
   IN  POS H_sparse[N0][PAD32(D)], 
   IN  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT])
{
   /* expand each syndome bit to u8 */
   uint8_t syndrome_bits[R_BITS];
   dense_to_u8(syndrome_bits, syndrome, R_BITS);
   /* vectorize H_sparse */
   __m256i v_H_sparse[N0][N_REGS_H];
   for (int block = 0; block < N0; block++) {
      for (int r = 0; r < N_REGS_H; r++) {
         v_H_sparse[block][r] = _mm256_loadu_si256((__m256i *)&H_sparse[block][r * 8]);
      }
   }
   /* compute unsatisfied parity checks */
   ALIGNED uint8_t upc[PAD8(N0 * R_BITS)] = {0};
   compute_upcs(upc, Htr_sparse, syndrome_bits);
   /* decoding iterations */
   int iter = 0;
   int hw = population_count(syndrome);
   DEBUG_PRINT("i: START \t hw(s): %d \n", hw);
   do {
      POS col = argmax_u8(upc);
      int col_block = col / R_BITS;
      int col_bit = col % R_BITS;
      gf2x_toggle_coeff(error + col_block * NUM_DIGITS_GF2X_ELEMENT, col_bit);
      hw = update_syndrome_and_upcs(upc, Htr_sparse, v_H_sparse, col, syndrome_bits, hw);
      DEBUG_PRINT("i: %3d \t hw(s): %d \n", iter, hw);
      iter++;
   } while ((iter < ITER_MAX_BFMAX) && (hw != 0));
   return 1;
}
////////////////////////////////////////////////////////////////////////////////
void decode_bfmax_internal(OUT e_t *e, IN const ct_t *ct, IN const sk_t *sk)
{
  DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT] = {0};
  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT] = {0};
  POS Htr_sparse[N0][PAD32(D)] = {0};
  POS H_sparse[N0][PAD32(D)] = {0};

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

  gf2x_mod_mul((pad_r_t *)&syndrome, &c0, &h0);

  bfmax_decoder(error, Htr_sparse, H_sparse, syndrome);

  for(int block=0; block<N0; block++) {
    memcpy(e->val[block].raw, error + block * NUM_DIGITS_GF2X_ELEMENT, R_BYTES);
  }
}