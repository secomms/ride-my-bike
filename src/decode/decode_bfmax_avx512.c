#include "decode_helpers.h"

#define N_REGS_UPC512      (N_REGS_UPC / 2)
#define N_REGS_UPC_TAIL     (N_REGS_UPC % 2)
#define I8_IN_ZMM             (512/8)

static INLINE POS argmax_avx512(uint8_t arr[PAD8(N0 * R_BITS)]) {
    /* find max */
 
    __m512i max_vec512 = _mm512_setzero_si512();
     int i = 0;
     for (; i < N_REGS_UPC512; i++) {
         __m512i v = _mm512_loadu_si512((const void *)&arr[i * I8_IN_ZMM]);
         max_vec512 = _mm512_max_epu8(max_vec512, v);
     }
     __m256i max_vec = _mm256_max_epu8(
         _mm512_castsi512_si256(max_vec512),
         _mm512_extracti64x4_epi64(max_vec512, 1)
     );
 
 #if N_REGS_UPC_TAIL
     /* AVX2 TAIL BEGIN: offset in byte = i * I8_IN_ZMM (fine dei blocchi da 64 byte),
      * NON i * I8_IN_YMM, perché qui i è ancora contato in unità ZMM */
     {
         __m256i v = _mm256_loadu_si256((__m256i *)&arr[i * I8_IN_ZMM]);
         max_vec = _mm256_max_epu8(max_vec, v);
     }
     /* AVX2 TAIL END */
 #endif
 
     /* extract max: riduzione 256 -> 8 bit (invariata) */
     __m128i lo = _mm256_castsi256_si128(max_vec);
     __m128i hi = _mm256_extracti128_si256(max_vec, 1);
     __m128i m = _mm_max_epu8(lo, hi);
     m = _mm_max_epu8(m, _mm_srli_si128(m, 8));
     m = _mm_max_epu8(m, _mm_srli_si128(m, 4));
     m = _mm_max_epu8(m, _mm_srli_si128(m, 2));
     m = _mm_max_epu8(m, _mm_srli_si128(m, 1));
     uint8_t max_val = (uint8_t)_mm_extract_epi8(m, 0);
 
     /* ---- find position of max (AVX-512, corpo principale) ---- */
     __m512i vmax512 = _mm512_set1_epi8((char)max_val);
     for (i = 0; i < N_REGS_UPC512; i++) {
         __m512i v = _mm512_loadu_si512((const void *)&arr[i * I8_IN_ZMM]);
         __mmask64 mask = _mm512_cmpeq_epi8_mask(v, vmax512);
         if (mask) {
             return (POS)(i * I8_IN_ZMM + __builtin_ctzll((unsigned long long)mask));
         }
     }
 
 #if N_REGS_UPC_TAIL
     /* AVX2 TAIL BEGIN: stesso discorso, offset = i * I8_IN_ZMM */
     {
         __m256i vmax = _mm256_set1_epi8((uint8_t)max_val);
         __m256i v = _mm256_loadu_si256((__m256i *)&arr[i * I8_IN_ZMM]);
         __m256i cmp = _mm256_cmpeq_epi8(v, vmax);
         int mask = _mm256_movemask_epi8(cmp);
         if (mask) {
             return (POS)(i * I8_IN_ZMM + __builtin_ctz(mask));
         }
     }
     /* AVX2 TAIL END */
 #endif
 
     return (POS)-1;
 }
 
 

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
      POS col = argmax_avx512(upc);
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

  bfmax_decoder(error, Htr_sparse, H_sparse, syndrome);

  for(int block=0; block<N0; block++) {
    memcpy(e->val[block].raw, error + block * NUM_DIGITS_GF2X_ELEMENT, R_BYTES);
  }
}
