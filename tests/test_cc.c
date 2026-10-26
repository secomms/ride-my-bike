
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
#include "stats.h"

////////////////////////////////////////////////////////////////////////////////

#define SEED 42

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {

  // argument parsing
  if (argc != 2) ERROR("Usage: %s <runs>\n", argv[0]);
  const uint64_t runs = atoi(argv[1]);

  cpu_features_init();

  srand(SEED);

  // clock cycles
  stats_t  stats = {0};
  uint64_t count_1;
  uint64_t count_2;
  uint64_t successes = 0;

  uint8_t sk[sizeof(sk_t)];
  uint8_t pk[sizeof(pk_t)];
  uint8_t ct[sizeof(ct_t)];
  uint8_t k_enc[sizeof(ss_t)];
  uint8_t k_dec[sizeof(ss_t)];

  for(uint64_t run = 0; run < runs; run++) {

    int res = 0;

    res = crypto_kem_keypair(pk, sk);
    if(res != 0) ERROR("keypair failed\n");

    res = crypto_kem_enc(ct, k_enc, pk);
    if(res != 0) ERROR("enc failed\n");

    count_1 = cpucycles();
    res = crypto_kem_dec(k_dec, ct, sk);
    count_2 = cpucycles();
    if(res != 0) ERROR("dec failed\n");

    int cmp = memcmp(k_enc, k_dec, sizeof(ss_t));
    if(cmp == 0) {
      successes++;
      stats_update(&stats, (double)(count_2 - count_1));
    }

  }

  // print csv results
  printf("decoder,%s,R,runs,successes,stddev,mean\n",CSV_STR);
  printf("%s," CSV_SPEC ",%d,%ld,%ld,%.2f,%.2f\n", CSV_DEC, CSV_PARAM, R_BITS, runs, successes, stats_stddev(&stats), stats_mean(&stats));

  return 0;
}
