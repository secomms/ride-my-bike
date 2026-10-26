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

void decode_oop_exotic_inner(OUT e_t *e, IN syndrome_t *s, IN const sk_t *sk, IN const pad_r_t  *c0, IN const pad_r_t *h0, IN const pad_r_t *pk, IN const decode_ctx * ctx)
{
   for(uint32_t iter = 0; iter < ITER_MAX_EXOTIC_BF - 1; iter++) {
     find_err(e, s, sk->wlist, (D+1)/2, ctx);
     recompute_syndrome(s, c0, h0, pk, e, ctx);
     if(r_bits_vector_weight((r_t*)s->qw) == 0) break;
   }
}

////////////////////////////////////////////////////////////////////////////////
POS argmax_u8_first_iteration(uint8_t upc[PAD8(N0 * R_BITS)], POS forbidden[MAX_RESETS_EXOTIC_BF], int f_len){
  uint8_t max = 0;
  POS best_pos = 0;

  for(POS i=0; i<N0*R_BITS; i++){
    if(max < upc[i]){
      int valid = 1;
      for(int j=0; j<f_len; j++){
        if(forbidden[j] == i){
          valid = 0;
          break;
        }
      }
      if(valid){
        max = upc[i];
        best_pos = i;
      }
    }
  }
  return best_pos;
}

int exotic_bfmax_decoder(
   OUT DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT], 
   IN  POS Htr_sparse[N0][PAD32(D)], 
   IN  POS H_sparse[N0][PAD32(D)], 
   IN  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT])
{
  /* vectorize H_sparse */
//   __m256i v_H_sparse[N0][N_REGS_H];
//   for (int block = 0; block < N0; block++) {
//      for (int r = 0; r < N_REGS_H; r++) {
//         v_H_sparse[block][r] = _mm256_loadu_si256((__m256i *)&H_sparse[block][r * 8]);
//      }
//   }
   /* expand each syndome bit to u8 */
   uint8_t syndrome_bits[R_BITS];
   uint8_t syndrome_bits_copy[R_BITS];
   dense_to_u8(syndrome_bits, syndrome, R_BITS);
   /* compute unsatisfied parity checks */

   int hw;

   static bfmax_ctx_t ctx;
   bfmax_ctx_init(&ctx, Htr_sparse, H_sparse);
   
   ALIGNED uint8_t upc[PAD8(N0 * R_BITS)] = {0};
   uint8_t upc_copy[PAD8(N0 * R_BITS)];
   compute_upcs(upc, Htr_sparse, syndrome_bits);

   memcpy(upc_copy,upc,sizeof(upc));
   memcpy(syndrome_bits_copy,syndrome_bits,sizeof(syndrome_bits));

   POS forbidden[MAX_RESETS_EXOTIC_BF];
   int resets = 0;
   int triggered_reset = 0;

   do{
    /* decoding iterations */
    int iter = 0;
    hw = population_count(syndrome);
    triggered_reset = 0;

    // First iteration
    POS col = argmax_u8_first_iteration(upc,forbidden,resets);
    uint8_t mx = upc[col];
    forbidden[resets] = col;
    
    int col_block = col / R_BITS;
    int col_bit = col % R_BITS;
    gf2x_toggle_coeff(error + col_block * NUM_DIGITS_GF2X_ELEMENT, col_bit);
    
    hw = bfmax_update(&ctx, upc, Htr_sparse, col, syndrome_bits, hw, &mx);
    DEBUG_PRINT("i: %3d \t hw(s): %d \n", iter, hw);
    iter++;

    do {
        col = bfmax_find(upc, mx);

        if(iter>=10 && upc[col] <= TH_RESET_EXOTIC_BF){
          triggered_reset = 1;
          break;
        }

        gf2x_toggle_coeff(error + (col / R_BITS) * NUM_DIGITS_GF2X_ELEMENT, col % R_BITS);
        hw = bfmax_update(&ctx, upc, Htr_sparse, col, syndrome_bits, hw, &mx);
        DEBUG_PRINT("i: %3d \t hw(s): %d \n", iter, hw);
        iter++;
    } while ((iter < ITER_MAX_EXOTIC_BFMAX) && (hw != 0));

    if(triggered_reset){
      memcpy(upc,upc_copy,sizeof(upc));
      memcpy(syndrome_bits,syndrome_bits_copy,sizeof(syndrome_bits));
      memset(error, 0, sizeof(DIGIT)*N0*NUM_DIGITS_GF2X_ELEMENT);
      resets++;
    }

   }while((hw != 0) && triggered_reset && resets < MAX_RESETS_EXOTIC_BF);

   int chosen_th = (D+1)/2;
   for (int i = 0; i < N0 * R_BITS; i++) {
      if (upc[i] >= chosen_th) {
         int col_block = i / R_BITS;
         int col_bit = i % R_BITS;
         gf2x_toggle_coeff(error + col_block * NUM_DIGITS_GF2X_ELEMENT, col_bit);
      }
   }

   return 1;
}

void decode_exotic_internal(OUT e_t *e, IN const ct_t *ct, IN const sk_t *sk)
{
   // BFMAX part

  DIGIT error[N0*NUM_DIGITS_GF2X_ELEMENT] = {0};
  DIGIT syndrome[NUM_DIGITS_GF2X_ELEMENT] = {0};
  POS Htr_sparse[N0][PAD32(D)];
  POS H_sparse[N0][PAD32(D)];

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

  exotic_bfmax_decoder(error, Htr_sparse, H_sparse, syndrome);

  for(int block=0; block<N0; block++) {
    memcpy(e->val[block].raw, error + block * NUM_DIGITS_GF2X_ELEMENT, R_BYTES);
    e->val[block].raw[R_BYTES-1] &= LAST_R_BYTE_MASK;
  }

  // OOP part
  decode_ctx ctx;
  decode_ctx_init(&ctx);

  pad_r_t pk = {0};

  // Pad public key (h)
  c0.val = ct->c0;
  h0.val = sk->bin[0];
  pk.val = sk->pk;
  
  DEFER_CLEANUP(syndrome_t s = {0}, syndrome_cleanup);
  DMSG("  Computing s.\n");
  recompute_syndrome(&s, &c0, &h0, &pk, e, &ctx);
  ctx.dup(&s);

  decode_oop_exotic_inner(e,&s,sk,&c0,&h0,&pk,&ctx);

}
