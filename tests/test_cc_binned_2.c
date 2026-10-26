
#include <assert.h>
#include <cpucycles.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu_features.h"
#include "decode_helpers.h"
#include "kem.h"

////////////////////////////////////////////////////////////////////////////////

#define SEED 42
#define LOW_BITS 7 // number of low bits to ignore when binning

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {

  // argument parsing
  if (argc != 2) ERROR("Usage: %s <runs>\n", argv[0]);
  const uint64_t runs = atoi(argv[1]);

  cpu_features_init();

  // results
  uint64_t* v_cc = calloc(runs, sizeof(*v_cc));
  uint64_t* f_cc = calloc(runs, sizeof(*f_cc));
  if(!v_cc || !f_cc) ERROR("calloc failed\n");
  uint64_t successes = 0;
  uint64_t count_1;
  uint64_t count_2;
  uint64_t f_count;
  uint64_t v_count;
  int res = 0;

  // kem
  uint8_t sk[sizeof(sk_t)];
  uint8_t pk[sizeof(pk_t)];
  uint8_t ct[sizeof(ct_t)];
  uint8_t k_enc[sizeof(ss_t)];
  uint8_t k_dec[sizeof(ss_t)];

  // fix e
  int f_seed_e = SEED;

  for(uint64_t run = 0; run < runs; run++) {

    int v_seed_e = SEED + run + 1;
    int seed_keypair = SEED + run + 2;

    srand(seed_keypair);
    res = crypto_kem_keypair(pk, sk);
    if(res != 0) ERROR("keypair failed\n");

    // use fixed e

    srand(f_seed_e);
    res = crypto_kem_enc(ct, k_enc, pk);
    if(res != 0) ERROR("enc failed\n");

    count_1 = cpucycles();
    res = crypto_kem_dec(k_dec, ct, sk);
    count_2 = cpucycles();
    if(res != 0) ERROR("dec failed\n");
    f_count = count_2 - count_1;
    int f_cmp = memcmp(k_enc, k_dec, sizeof(ss_t));

    // use variable e

    srand(v_seed_e);
    res = crypto_kem_enc(ct, k_enc, pk);
    if(res != 0) ERROR("enc failed\n");

    count_1 = cpucycles();
    res = crypto_kem_dec(k_dec, ct, sk);
    count_2 = cpucycles();
    if(res != 0) ERROR("dec failed\n");
    v_count = count_2 - count_1;
    int v_cmp = memcmp(k_enc, k_dec, sizeof(ss_t));

    // save results

    if((v_cmp == 0) && (f_cmp == 0)) {
      f_cc[successes] = f_count >> LOW_BITS;
      v_cc[successes] = v_count >> LOW_BITS;
      successes++;
    }
  }

  // print info
  printf("decoder,%s,R,runs,successes,LOW_BITS\n",CSV_STR);
  printf("%s," CSV_SPEC ",%d,%ld,%ld,%d\n", CSV_DEC, CSV_PARAM, R_BITS, runs, successes, LOW_BITS);
  printf("--------\n");

  // print results
  for(uint64_t r = 0; r < successes; r++) {
    printf("%lu,%lu\n", f_cc[r], v_cc[r]);
  }

  free(v_cc);
  free(f_cc);

  return 0;
}
