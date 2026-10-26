 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <signal.h>
 #include <math.h>
 #include <inttypes.h>
 #include <unistd.h>
 
 #include "gf2x.h"
 #include "kem.h"
 #include "decode.h"
 #include "sampling.h"
 #include "measurements.h"
 #include "utilities.h"
 #include "cpu_features.h"
 #include "defs.h" /* for R_BITS; adjust the path if your tree keeps it
                     * elsewhere (it's usually pulled in transitively by
                     * kem.h/types.h anyway, this include is just to be
                     * explicit about the dependency) */
 
 /* ---------------------------------------------------------------------- */
 /* Configuration                                                          */
 /* ---------------------------------------------------------------------- */
 
 /* Identifies which decoder variant this binary was built with. Not
  * auto-detected: pass it at compile time, e.g.
  *   -DDECODER_NAME=\"BGF\"
  * or replace this block with #ifdef checks on whatever macro your fork
  * actually uses to select a decoder (e.g. #if defined(BGF_DECODER) ...). */
 #ifndef DECODER_NAME
 #  define DECODER_NAME "unknown (set via -DDECODER_NAME=\"...\")"
 #endif
 
 #ifndef NUM_OF_TESTS
 #  define NUM_OF_TESTS 100000ULL /* 10^5 */
 #endif
 
 #ifndef REPORT_INTERVAL
 #  define REPORT_INTERVAL 10000ULL
 #endif

 #define SEED 42
 
 
 /* ---------------------------------------------------------------------- */
 /* Magic-number guarded buffers (same pattern as the original clock-cycle */
 /* test)                                                                  */
 /* ---------------------------------------------------------------------- */
 
 typedef struct magic_number_s {
   uint64_t val[4];
 } magic_number_t;
 
 #define STRUCT_WITH_MAGIC(name, size) \
   struct {                            \
     magic_number_t magic1;            \
     uint8_t        val[size];         \
     magic_number_t magic2;            \
   }(name) = {magic, {0}, magic};
 
 #define CHECK_MAGIC(param)                                                \
   if((0 != memcmp((param).magic1.val, magic.val, sizeof(magic))) ||       \
      (0 != memcmp((param).magic2.val, magic.val, sizeof(magic)))) {       \
     fprintf(stderr,                                                       \
             "Magic is incorrect for " #param                              \
             " (memory overflow) at trial %" PRIu64 "\n",                  \
             trial);                                                       \
     exit(EXIT_FAILURE);                                                   \
   }
 

    /* All progress/diagnostic output goes through DEBUG_LOG, which is a
  * no-op unless DEBUG is defined at compile time (e.g. -DDEBUG=1 or
  * plain -DDEBUG). Actual errors (keypair failure, magic-number
  * corruption) are NOT gated by this and always print, since those are
  * failures the caller needs to see regardless of DEBUG. Everything
  * else (banner, progress reports, per-trial failure notices,
  * done/interrupted messages) is gated, so with DEBUG off, stdout
  * carries only the final CSV line and stderr stays silent too. */
 #ifdef DEBUG
 #  define DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
 #else
 #  define DEBUG_LOG(...) do { } while(0)
 #endif


 /* Fixed field widths so columns stay aligned as counters grow, instead
  * of the whole line shifting left/right with the number of digits.
  * STR(x) turns a numeric macro into a string literal usable inside a
  * printf format string, e.g. "%" STR(W_TRIAL) PRIu64 -> "%12" PRIu64. */
 #define STR_HELPER(x) #x
 #define STR(x)        STR_HELPER(x)
 
 #define W_TRIAL 13 /* wide enough for up to ~10^13 trials */
 #define W_FAIL  9  /* wide enough for up to ~10^9 failures */
 #define W_PID   8
 #define W_SEED  10
 
 #ifdef DEBUG
 static void report_progress(uint64_t trial, uint64_t failures, uint64_t total)
 {
   double dfr = (trial > 0) ? ((double)failures / (double)trial) : 0.0;
   double se  = (trial > 0) ? sqrt(dfr * (1.0 - dfr) / (double)trial) : 0.0;
   DEBUG_LOG("[%" STR(W_TRIAL) PRIu64 " / %" STR(W_TRIAL) PRIu64
          "]  failures = %" STR(W_FAIL) PRIu64
          "  DFR = %.6e  (+/- %.2e, 1-sigma)\n",
          trial, total, failures, dfr, se);
   fflush(stderr);
 }
 #endif
 
 /* ---------------------------------------------------------------------- */
 /* Ctrl-C handling                                                        */
 /* ---------------------------------------------------------------------- */
 
 static volatile sig_atomic_t g_stop_requested = 0;
 
 static void handle_sigint(int sig)
 {
   (void)sig;
   g_stop_requested = 1;
 }
 
 /* ---------------------------------------------------------------------- */
 /* Main                                                                    */
 /* ---------------------------------------------------------------------- */
 
 int main(int argc, char *argv[])
 {
   uint64_t num_trials  = NUM_OF_TESTS;
 
   if(argc >= 2) {
     num_trials = strtoull(argv[1], NULL, 10);
   }
 
   cpu_features_init();
 
   srand(SEED);
 
   signal(SIGINT, handle_sigint);
 
   uint64_t trial    = 0;
   uint64_t failures = 0;
 
   magic_number_t magic = {0xa1234567b1234567, 0xc1234567d1234567,
                            0xe1234567f1234567, 0x0123456711234567};
   STRUCT_WITH_MAGIC(sk, sizeof(sk_t));
   STRUCT_WITH_MAGIC(pk, sizeof(pk_t));
   STRUCT_WITH_MAGIC(ct, sizeof(ct_t));
   STRUCT_WITH_MAGIC(k_enc, sizeof(ss_t)); /* shared secret from encaps */
   STRUCT_WITH_MAGIC(k_dec, sizeof(ss_t)); /* shared secret from decaps */
 
   /* ---- Generate ONE keypair, reused by every trial ---- */
   int kg_rc = crypto_kem_keypair(pk.val, sk.val);
   if(kg_rc != 0) {
     fprintf(stderr, "Keypair generation failed (%d)\n", kg_rc);
     return EXIT_FAILURE;
   }
   CHECK_MAGIC(sk);
   CHECK_MAGIC(pk);
 
 
   DEBUG_LOG("==========================================================\n");
   DEBUG_LOG(" BIKE Decoding Failure Rate (DFR) test\n");
   DEBUG_LOG("==========================================================\n");
   DEBUG_LOG("  R_BITS       : %" STR(W_TRIAL) "d\n", R_BITS);
   //DEBUG_LOG("  Decoder      : %" STR(W_TRIAL) "s\n", DECODER_NAME);
   DEBUG_LOG("  Target trials: %" STR(W_TRIAL) PRIu64 "\n", num_trials);
   //DEBUG_LOG("  PID          : %" STR(W_PID) "d\n", (int)getpid());
   //DEBUG_LOG("  Seed offset  : %" STR(W_SEED) "u\n", seed_offset);
   DEBUG_LOG("==========================================================\n\n");
   fflush(stderr);
 
   e_t e_out; /* diagnostic only, from decode() on failing trials */
 
   while(trial < num_trials && !g_stop_requested) {
 
     int enc_rc = crypto_kem_enc(ct.val, k_enc.val, pk.val);
     if(enc_rc != 0) {
       fprintf(stderr,
               "Encapsulate failed (%d) at trial %" STR(W_TRIAL) PRIu64 "\n",
               enc_rc, trial);
       continue;
     }
 
     uint32_t dec_rc = crypto_kem_dec(k_dec.val, ct.val, sk.val);
 
     int failure =
       (dec_rc != 0) ||
       !secure_cmp(k_enc.val, k_dec.val,
                   sizeof(k_dec.val) / sizeof(uint64_t));
 
     if(failure) {
       failures++;
       /* Diagnostic only: raw decoder output on this ciphertext. Not
        * compared against ground truth (we don't have the sampled e). */
       decode(&e_out, (ct_t *)ct.val, (sk_t *)sk.val);
       DEBUG_LOG("*** Decoding FAILURE at trial %" STR(W_TRIAL) PRIu64
              "  (dec_rc = %3u, failures so far = %" STR(W_FAIL) PRIu64
              ")\n",
              trial + 1, dec_rc, failures);
     }
 
     /* Check magic numbers (memory overflow) on every trial */
     CHECK_MAGIC(sk);
     CHECK_MAGIC(pk);
     CHECK_MAGIC(ct);
     CHECK_MAGIC(k_enc);
     CHECK_MAGIC(k_dec);
 
     trial++;
 
     if(trial % REPORT_INTERVAL == 0) {
      #ifdef DEBUG
       report_progress(trial, failures, num_trials);
       #endif
     }
   }
 

    #ifdef DEBUG
   report_progress(trial, failures, num_trials);
   #endif
 
   if(g_stop_requested) {
     DEBUG_LOG("Interrupted by user.");
   } else if(trial > 0) {
     DEBUG_LOG("Done.  trials = %" STR(W_TRIAL) PRIu64 "  failures = %"
            STR(W_FAIL) PRIu64 "  DFR = %.6e\n",
            trial, failures, (double)failures / (double)trial);
   }

   double final_dfr = (trial > 0) ? ((double)failures / (double)trial) : 0.0;
   printf("dfr, %d, %" PRIu64 ", %" PRIu64 ", %.9f\n",
          R_BITS, trial, failures, final_dfr);
 
 
   return 0;
 }