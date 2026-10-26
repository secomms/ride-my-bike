
#include <stdint.h>
#include <immintrin.h>
#include <stdalign.h>
#include <stdio.h>

#include "decode.h"
#include "decode_internal.h"
#include "utilities.h"
#include "gf2x.h"


#define INLINE inline
#define RESTRICT
#define CONST
////////////////////////////////////////////////////////////////////////////////
#define DEBUG 0
#if DEBUG
#define DEBUG_PRINT(...)                            \
    do {                                            \
        printf("\033[0;33m [@] [%s]\t ", __func__); \
        printf(__VA_ARGS__);                        \
        printf("\033[0m");                          \
    } while(0)
#else
#define DEBUG_PRINT(...) ((void)0)
#endif
#define ALIGNED alignas(32)
////////////////////////////////////////////////////////////////////////////////
#define DIGIT                 uint64_t
#define SIGNED_DIGIT          int64_t
#define DIGIT_SIZE_B          8
#define DIGIT_SIZE_b          (DIGIT_SIZE_B << 3)
#define POS            uint32_t
#define SIGNED_POS     int32_t
////////////////////////////////////////////////////////////////////////////////
#ifndef ITER_MAX_BFMAX
#define ITER_MAX_BFMAX          ((int)(1.5 * T))
#endif
//------------------------------------------------------------------------------
#ifndef ITER_MAX_BF
#define ITER_MAX_BF             (5)
#endif
//------------------------------------------------------------------------------
#ifndef ITER_MAX_HYBRID_BFMAX
#define ITER_MAX_HYBRID_BFMAX   ((int)(0.3 * T))
#endif
#ifndef ITER_MAX_HYBRID_BF
#define ITER_MAX_HYBRID_BF      (3)
#endif
//------------------------------------------------------------------------------
#ifndef ITER_MAX_EXOTIC_BFMAX
#define ITER_MAX_EXOTIC_BFMAX   ((int)(0.4 * T))
#endif
#ifndef ITER_MAX_EXOTIC_BF
#define ITER_MAX_EXOTIC_BF      (3)
#endif
#ifndef TH_RESET_EXOTIC_BF
#define TH_RESET_EXOTIC_BF      (42)
#endif
#ifndef MAX_RESETS_EXOTIC_BF
#define MAX_RESETS_EXOTIC_BF    (50)
#endif
////////////////////////////////////////////////////////////////////////////////
#define ERROR(...)                                  \
    do {                                            \
        printf("\n[!] [%s] ", __func__);            \
        printf(__VA_ARGS__);                        \
        printf("\n");                               \
        exit(1);                                    \
    } while(0)
////////////////////////////////////////////////////////////////////////////////
#if defined(DECODER_BFMAX)
  #define CSV_DEC               "BFMAX"
  #define CSV_PARAM             ITER_MAX_BFMAX
  #define CSV_STR               "ITER_MAX_BFMAX"
  #define CSV_SPEC              "%d"
#elif defined (DECODER_BGF)
  #define CSV_DEC               "BGF"
  #define CSV_PARAM             DECODER_NUM_ITER
  #define CSV_STR               "DECODER_NUM_ITER"
  #define CSV_SPEC              "%d"
#elif defined (DECODER_HYBRID)
  #define CSV_DEC               "HYBRID"
  #define CSV_PARAM             ITER_MAX_HYBRID_BFMAX,ITER_MAX_HYBRID_BF
  #define CSV_STR               "ITER_MAX_HYBRID_BFMAX,ITER_MAX_HYBRID_BF"
  #define CSV_SPEC              "%d,%d"
#elif defined (DECODER_OOP)
  #define CSV_DEC               "OOP"
  #define CSV_PARAM             ITER_MAX_BF
  #define CSV_STR               "ITER_MAX_BF"
  #define CSV_SPEC              "%d"
#elif defined (DECODER_EXOTIC)
  #define CSV_DEC               "EXOTIC"
  #define CSV_PARAM             ITER_MAX_EXOTIC_BFMAX,ITER_MAX_EXOTIC_BF,TH_RESET_EXOTIC_BF,MAX_RESETS_EXOTIC_BF
  #define CSV_STR               "ITER_MAX_EXOTIC_BFMAX,ITER_MAX_EXOTIC_BF,TH_RESET_EXOTIC_BF,MAX_RESETS_EXOTIC_BF"
  #define CSV_SPEC              "%d,%d,%d,%d"
#else
  #error "No decoder defined"
#endif
////////////////////////////////////////////////////////////////////////////////
#define TH0 43
#define TH1 38
#define TH2 36
////////////////////////////////////////////////////////////////////////////////
/* number of 32-bit integers that fit inside a YMM register */
#define I32_IN_YMM            (256/32)
/* number of 8-bit integers that fit inside a YMM register */
#define I8_IN_YMM             (256/8)
////////////////////////////////////////////////////////////////////////////////
/* round up the size of an array of 32-bit integers to fit inside YMM regs */
#define PAD32(x)              (((x) + I32_IN_YMM - 1) & ~(I32_IN_YMM - 1))
/* round up the size of an array of 8-bit integers to fit inside YMM regs */
#define PAD8(x)               (((x) + I8_IN_YMM - 1) & ~(I8_IN_YMM - 1))
////////////////////////////////////////////////////////////////////////////////
#define                NUM_BITS_GF2X_ELEMENT (R_BITS)
#define              NUM_DIGITS_GF2X_ELEMENT ((R_BITS+DIGIT_SIZE_b-1)/DIGIT_SIZE_b)
#define MSb_POSITION_IN_MSB_DIGIT_OF_ELEMENT ( (R_BITS % DIGIT_SIZE_b) ? (R_BITS % DIGIT_SIZE_b)-1 : DIGIT_SIZE_b-1 )
#define                NUM_BITS_GF2X_MODULUS (R_BITS+1)
#define              NUM_DIGITS_GF2X_MODULUS ((R_BITS+1+DIGIT_SIZE_b-1)/DIGIT_SIZE_b)
#define MSb_POSITION_IN_MSB_DIGIT_OF_MODULUS (R_BITS-DIGIT_SIZE_b*(NUM_DIGITS_GF2X_MODULUS-1))
#define                    INVALID_POS_VALUE (R_BITS)
////////////////////////////////////////////////////////////////////////////////
#define N_REGS_H   (PAD32(D) / I32_IN_YMM)
#define N_REGS_UPC (PAD8(N0 * R_BITS) / I8_IN_YMM)
////////////////////////////////////////////////////////////////////////////////
static INLINE int population_count(DIGIT upc[])
{
   int ret = 0;
   for(int i = NUM_DIGITS_GF2X_ELEMENT - 1; i >= 0; i--) {
      ret += __builtin_popcountll((unsigned long long int) (upc[i]));
   }
   return ret;
} // end population_count
////////////////////////////////////////////////////////////////////////////////
static INLINE DIGIT gf2x_get_coeff(CONST DIGIT poly[], CONST unsigned int exponent)
{
   unsigned int digitIdx = exponent / DIGIT_SIZE_b;
   unsigned int inDigitIdx = exponent % DIGIT_SIZE_b;
   return (poly[digitIdx] >> inDigitIdx) & ((DIGIT) 1);
}
////////////////////////////////////////////////////////////////////////////////
static INLINE void gf2x_toggle_coeff(DIGIT poly[], CONST unsigned int exponent)
{
   int digitIdx = exponent / DIGIT_SIZE_b;
   unsigned int inDigitIdx = exponent % DIGIT_SIZE_b;
   /* clear given coefficient */
   DIGIT mask = ( ((DIGIT) 1) << inDigitIdx);
   poly[digitIdx] = poly[digitIdx] ^ mask;
}
////////////////////////////////////////////////////////////////////////////////
static INLINE POS argmax_u8(uint8_t arr[PAD8(N0 * R_BITS)]) {
   /* find max */
   __m256i max_vec = _mm256_setzero_si256();
   for (int i = 0; i < N_REGS_UPC; i++) {
      __m256i v = _mm256_loadu_si256((__m256i *)&arr[i * I8_IN_YMM]);
      max_vec = _mm256_max_epu8(max_vec, v);
   }
   /* extract max from YMM using horizontal reduction */
   __m128i lo = _mm256_castsi256_si128(max_vec);
   __m128i hi = _mm256_extracti128_si256(max_vec, 1);
   __m128i m = _mm_max_epu8(lo, hi);
   m = _mm_max_epu8(m, _mm_srli_si128(m, 8));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 4));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 2));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 1));
   uint8_t max_val = (uint8_t)_mm_extract_epi8(m, 0);
   /* find position of max */
   __m256i vmax = _mm256_set1_epi8((uint8_t)max_val);
   for (int i = 0; i < N_REGS_UPC; i++) {
      __m256i v = _mm256_loadu_si256((__m256i *)&arr[i * I8_IN_YMM]);
      __m256i cmp = _mm256_cmpeq_epi8(v, vmax);
      int mask = _mm256_movemask_epi8(cmp);
      if (mask) {
         return (POS)(i * I8_IN_YMM + __builtin_ctz(mask));
      }
   }
   return (POS)-1;
}
////////////////////////////////////////////////////////////////////////////////
static INLINE int update_syndrome_and_upcs(
   OUT uint8_t upc[PAD8(N0 * R_BITS)], 
   IN  POS Htr_sparse[N0][PAD32(D)], 
   IN  __m256i v_H_sparse[N0][N_REGS_H],
   IN  POS flip, 
   OUT uint8_t syndrome_bits[R_BITS],
   IN  int hw)
{
   int flip_block = flip / R_BITS;
   int flip_bit = flip - flip_block * R_BITS;
   __m256i vp = _mm256_set1_epi32((uint32_t)R_BITS);
   __m256i vpos = _mm256_set1_epi32((uint32_t)flip_bit);
   /* update syndrome and save upc positions to update */
   __m256i up_pos[D][N0][N_REGS_H];
   int up_sign[D];
   for (int col_reg = 0; col_reg < N_REGS_H; col_reg++) {
      /* get the column of H corresponding to the flipped bit */
      uint32_t tmp[I32_IN_YMM];
      __m256i htr = _mm256_loadu_si256((__m256i *)&Htr_sparse[flip_block][col_reg * I32_IN_YMM]);
      __m256i sum = _mm256_add_epi32(htr, vpos);
      __m256i sub = _mm256_sub_epi32(sum, vp);
      __m256i res = _mm256_min_epu32(sum, sub);
      _mm256_storeu_si256((__m256i *)tmp, res);
      /* scan each idx in the column */
      for (int i = 0; (i < I32_IN_YMM) && (col_reg * I32_IN_YMM + i < D); i++) {
         /* update the syndrome */
         POS row = tmp[i];
         syndrome_bits[row] ^= 1;
         int delta = (syndrome_bits[row] == 0) ? -1 : 1;
         hw += delta;
         if (hw == 0) {
            return hw;
         }
         up_sign[col_reg * I32_IN_YMM + i] = delta;
         /* save upc positions to update (faster than updating upcs directly) */
         __m256i vrow = _mm256_set1_epi32((uint32_t)row);
         for (int block = 0; block < N0; block++) {
            for (int row_reg = 0; row_reg < N_REGS_H; row_reg++) {
               __m256i col = _mm256_add_epi32(v_H_sparse[block][row_reg], vrow);
               __m256i sub2 = _mm256_sub_epi32(col, vp);
               __m256i res2 = _mm256_min_epu32(col, sub2);
               up_pos[col_reg * I32_IN_YMM + i][block][row_reg] = res2;
            }
         }
      }
   }
   /* update upcs */
   uint32_t *RESTRICT up_pos_u32 = (uint32_t *)up_pos;
   for (int i = 0; i < D; i++) {
      int delta = up_sign[i];
      for (int block = 0; block < N0; block++) {
         for (int row_reg = 0; row_reg < N_REGS_H; row_reg++) {
            for (int lane = 0; (lane < 8) && (row_reg * 8 + lane < D); lane++) {
               int up_idx = (((i * N0 + block) * N_REGS_H + row_reg) * 8) + lane;
               uint32_t col = up_pos_u32[up_idx];
               upc[block * R_BITS + col] += delta;
            }
         }
      }
   }
   return hw;
}
////////////////////////////////////////////////////////////////////////////////
/* for r in rows
 *   for c in columns
 *     upc[c] += s[r] & H[r][c]
 */
static INLINE void compute_upcs(
   OUT uint8_t upc[PAD8(N0 * R_BITS)],
   IN  POS Htr_sparse[N0][PAD32(D)],
   IN  uint8_t syndrome_bits[R_BITS])
{
   /* for each block */
   for (int block = 0; block < N0; block++) {
      uint8_t *upc_block = &upc[block * R_BITS];
      /* scan each idx in the first column */
      for (int i = 0; i < D; i++) {
         int idx = Htr_sparse[block][i];
         int wrap = R_BITS - idx;
         /* increment idx to shift the column (first half of upcs) */
         for (int j = 0; j < wrap; j++) {
            upc_block[j] += syndrome_bits[idx + j];
         }
         /* increment idx to shift the column (second half of upcs) */
         for (int k = wrap; k < wrap + idx; k++) {
            upc_block[k] += syndrome_bits[k - wrap];
         }
      }
   }
}
////////////////////////////////////////////////////////////////////////////////
static INLINE void dense_to_u8(
   OUT uint8_t u8[],
   IN  DIGIT dense[],
   IN  int len)
{
   for (int i = 0; i < len; i++) {
      u8[i] = gf2x_get_coeff(dense, i);
   }
}
////////////////////////////////////////////////////////////////////////////////
static INLINE void u8_to_dense(
   OUT DIGIT dense[],
   IN  uint8_t u8[],
   IN  int len)
{
   memset(dense, 0, NUM_DIGITS_GF2X_ELEMENT * DIGIT_SIZE_B);
   for (int i = 0; i < len; i++) {
      if (u8[i]) {
         gf2x_toggle_coeff(dense, i);
      }
   }
}

#if defined(__AVX2__)
////////////////////////////////////////////////////////////////////////////////
/* FAST BF-MAX UPDATE (autocorrelation table + minority-sign scatter)
 *
 * Uses the BIKE names directly (R_BITS, D, N0, POS) and the padded row
 * stride PAD32(D) of Htr_sparse / H_sparse. The upc array is the padded
 * PAD8(N0 * R_BITS) buffer; the padding bytes are never read or written.
 */
#define BFX_HPAD   PAD32(D)                      /* H rows padded to a multiple of 8 */
#define BFX_ALEN   (R_BITS + 64)                 /* table + 32-byte wrap pad (+slack) */
#define BFX_PV     ((R_BITS / 32) * 32)

typedef struct {
   ALIGNED uint8_t  A[N0][N0][BFX_ALEN];
   ALIGNED POS      H[N0][BFX_HPAD];
   ALIGNED uint16_t idx[N0 * D * BFX_HPAD + 32];
} bfmax_ctx_t;

_Static_assert(N0 * R_BITS <= 65535, "uint16 scatter indices need N0*R_BITS < 2^16");

/* Once per key (~4*D*D increments). Can be stored with the private key. */
static INLINE void bfmax_ctx_init(
   OUT bfmax_ctx_t *ctx,
   IN  POS Htr_sparse[N0][PAD32(D)],
   IN  POS H_sparse[N0][PAD32(D)])
{
   memset(ctx->A, 0, sizeof ctx->A);
   for (int b = 0; b < N0; b++) {
      for (int b2 = 0; b2 < N0; b2++) {
         uint8_t *A = ctx->A[b][b2];
         for (int k = 0; k < D; k++) {
            for (int m = 0; m < D; m++) {
               uint32_t t = Htr_sparse[b][k] + H_sparse[b2][m];
               t -= (t >= R_BITS) ? R_BITS : 0;
               A[t]++;
            }
         }
         memcpy(A + R_BITS, A, 32);   /* wrap pad: a vector may straddle the rotation point */
      }
   }
   /* own copy with zeroed padding lanes, independent of the caller's padding */
   memset(ctx->H, 0, sizeof ctx->H);
   for (int b2 = 0; b2 < N0; b2++) {
      memcpy(ctx->H[b2], H_sparse[b2], D * sizeof(POS));
   }
}
////////////////////////////////////////////////////////////////////////////////
static INLINE uint8_t bfx_hmax(__m256i v)
{
   __m128i m = _mm_max_epu8(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 8));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 4));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 2));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 1));
   return (uint8_t)_mm_cvtsi128_si32(m);
}
////////////////////////////////////////////////////////////////////////////////
/* full max over the N0*R_BITS real counters (padding excluded) */
static INLINE uint8_t bfmax_max(IN const uint8_t upc[PAD8(N0 * R_BITS)])
{

   __m256i a0 = _mm256_setzero_si256();
   __m256i a1 = a0;
   /* ------------------------------------- Scan with two register ---------------------------------------- */
   /* a0 contains the maximum between odds registers   */
   /* a1 contains the maximum between events registers */
   /* With two acculmulator we broke the sequential dependency */
   int i = 0;
   for (; i + 1 < N_REGS_UPC; i += 2) {
      a0 = _mm256_max_epu8(a0, _mm256_loadu_si256((const __m256i *)&upc[i * 32]));
      a1 = _mm256_max_epu8(a1, _mm256_loadu_si256((const __m256i *)&upc[(i + 1) * 32]));
   }
   /* This is necessary because otherwise the register 771 will be not included */ 
   if (i < N_REGS_UPC) a0 = _mm256_max_epu8(a0, _mm256_loadu_si256((const __m256i *)&upc[i * 32]));
    
   /* -------------------------------------- Horizontal reduction ----------------------------------------- */
   a0 = _mm256_max_epu8(a0, a1);
   __m128i m = _mm_max_epu8(_mm256_castsi256_si128(a0), _mm256_extracti128_si256(a0, 1));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 8));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 4));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 2));
   m = _mm_max_epu8(m, _mm_srli_si128(m, 1));
   return (uint8_t)_mm_extract_epi8(m, 0);
}
////////////////////////////////////////////////////////////////////////////////
/* first index whose counter equals mx (same tie-breaking as argmax_u8) */
static INLINE POS bfmax_find(IN const uint8_t upc[PAD8(N0 * R_BITS)], IN uint8_t mx)
{
   const __m256i v = _mm256_set1_epi8((char)mx);
   int i = 0;
   for (; i + 64 <= N0 * R_BITS; i += 64) {
      uint64_t lo = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(upc + i)), v));
      uint64_t hi = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(upc + i + 32)), v));
      uint64_t m = lo | (hi << 32);
      if (m) return (POS)(i + __builtin_ctzll(m));
   }
   for (; i < N0 * R_BITS; i++) {
      if (upc[i] == mx) return (POS)i;
   }
   return (POS)-1;
}
////////////////////////////////////////////////////////////////////////////////
/* sb[j] -= A[(j-pos) mod r]  (or += when !neg); returns running max */
static INLINE __m256i bfx_dense(uint8_t *sb, const uint8_t *A, uint32_t pos, int neg,
                                __m256i vmax, uint8_t *smax)
{
   const uint8_t *s1 = A + R_BITS - pos;   /* j <  pos; overrun into the wrap pad is correct */
   const uint8_t *s2 = A - pos;            /* j >= pos */
   uint32_t split = (pos + 31) & ~31u;
   if (split > BFX_PV) split = BFX_PV;
   uint32_t j = 0;
   if (neg) {
      for (; j < split; j += 32) {
         __m256i x = _mm256_sub_epi8(_mm256_loadu_si256((__m256i *)(sb + j)), _mm256_loadu_si256((const __m256i *)(s1 + j)));
         _mm256_storeu_si256((__m256i *)(sb + j), x); vmax = _mm256_max_epu8(vmax, x);
      }
      for (; j < BFX_PV; j += 32) {
         __m256i x = _mm256_sub_epi8(_mm256_loadu_si256((__m256i *)(sb + j)), _mm256_loadu_si256((const __m256i *)(s2 + j)));
         _mm256_storeu_si256((__m256i *)(sb + j), x); vmax = _mm256_max_epu8(vmax, x);
      }
   } else {
      for (; j < split; j += 32) {
         __m256i x = _mm256_add_epi8(_mm256_loadu_si256((__m256i *)(sb + j)), _mm256_loadu_si256((const __m256i *)(s1 + j)));
         _mm256_storeu_si256((__m256i *)(sb + j), x); vmax = _mm256_max_epu8(vmax, x);
      }
      for (; j < BFX_PV; j += 32) {
         __m256i x = _mm256_add_epi8(_mm256_loadu_si256((__m256i *)(sb + j)), _mm256_loadu_si256((const __m256i *)(s2 + j)));
         _mm256_storeu_si256((__m256i *)(sb + j), x); vmax = _mm256_max_epu8(vmax, x);
      }
   }
   for (; j < R_BITS; j++) {   /* R_BITS % 32 tail, stays inside the block */
      uint32_t t = j + R_BITS - pos;
      t -= (t >= R_BITS) ? R_BITS : 0;
      sb[j] = neg ? (uint8_t)(sb[j] - A[t]) : (uint8_t)(sb[j] + A[t]);
      if (sb[j] > *smax) *smax = sb[j];
   }
   return vmax;
}
////////////////////////////////////////////////////////////////////////////////
/* Apply the flip (in [0, N0*R_BITS)): updates syndrome_bits and upc, returns
 * the new syndrome weight and writes the new max counter to *new_max. */
static INLINE int bfmax_update(
   OUT   bfmax_ctx_t *ctx,
   OUT   uint8_t upc[PAD8(N0 * R_BITS)],
   IN    POS Htr_sparse[N0][PAD32(D)],
   IN    POS flip,
   OUT   uint8_t syndrome_bits[R_BITS],
   IN    int hw,
   OUT   uint8_t *new_max)
{
   const int b = flip >= R_BITS;
   const uint32_t pos = flip - b * R_BITS;

   /* 1. toggle the D syndrome bits, split rows by their new value */
   uint32_t rows[2][D];
   int cnt[2] = {0, 0};
   for (int k = 0; k < D; k++) {
      uint32_t r = Htr_sparse[b][k] + pos;
      r -= (r >= R_BITS) ? R_BITS : 0;
      uint8_t s = (syndrome_bits[r] ^= 1);
      rows[s][cnt[s]++] = r;
   }
   hw += cnt[1] - cnt[0];

   /* 2. minority sign -> scatter, majority -> dense table */
   const int neg = cnt[1] <= cnt[0];   /* usual case: dense -A, scatter +2 on newly unsat rows */
   const uint32_t *sr = rows[neg];
   const int ns = cnt[neg];
   const uint8_t d2 = neg ? 2 : (uint8_t)-2;

   /* 3a. all scatter targets with AVX2 into a uint16 buffer ... */
   const __m256i vR = _mm256_set1_epi32(R_BITS);
   uint16_t *idx = ctx->idx;
   int n = 0;
   for (int b2 = 0; b2 < N0; b2++) {
      const __m256i voff = _mm256_set1_epi32(b2 * R_BITS);
      for (int i = 0; i < ns; i++) {
         const __m256i vr = _mm256_set1_epi32(sr[i]);
         for (int m = 0; m < BFX_HPAD; m += 8) {
            __m256i c = _mm256_add_epi32(_mm256_load_si256((const __m256i *)&ctx->H[b2][m]), vr);
            c = _mm256_min_epu32(c, _mm256_sub_epi32(c, vR));   /* mod r */
            c = _mm256_add_epi32(c, voff);
            __m128i p = _mm_packus_epi32(_mm256_castsi256_si128(c), _mm256_extracti128_si256(c, 1));
            _mm_storeu_si128((__m128i *)&idx[n + m], p);   /* lanes >= D are overwritten / ignored */
         }
         n += D;
      }
   }
   /* 3b. ... then a tight byte read-modify-write loop */
   int i = 0;
   for (; i + 4 <= n; i += 4) {
      upc[idx[i]] += d2;     upc[idx[i + 1]] += d2;
      upc[idx[i + 2]] += d2; upc[idx[i + 3]] += d2;
   }
   for (; i < n; i++) upc[idx[i]] += d2;

   /* 4. dense rotated table, fused with the max for the next iteration */
   __m256i vmax = _mm256_setzero_si256();
   uint8_t smax = 0;
   for (int b2 = 0; b2 < N0; b2++) {
      vmax = bfx_dense(upc + b2 * R_BITS, ctx->A[b][b2], pos, neg, vmax, &smax);
   }
   uint8_t mv = bfx_hmax(vmax);
   *new_max = mv > smax ? mv : smax;
   return hw;
}

#endif
