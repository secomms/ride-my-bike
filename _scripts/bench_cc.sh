#!/usr/bin/env bash
#
# For each prime in a list, build the cryptosystem and measure the average
# clock cycles taken for decapsulation, saving the results to a csv file.
#
# Usage:
#   ./_scripts/cc.sh <DECODER> <RUNS> <PRIMES_FILE> [NAME=VALUE ...]
#
# Examples:
#   ./_scripts/cc.sh BFMAX 1000 _docs/primes/test_primes.txt ITER_MAX_BFMAX=200
#   ./_scripts/cc.sh HYBRID 1000 _docs/primes/test_primes.txt \
#       ITER_MAX_HYBRID_BFMAX=200 ITER_MAX_HYBRID_BF=50
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common.sh"

CPU_CORE=0   # pin the clock cycle measurement to this core

usage() {
    cat >&2 <<EOF
Usage: $(basename "$0") <DECODER> <RUNS> <PRIMES_FILE> [NAME=VALUE ...]

  DECODER              decoder da usare (case-insensitive)
                        valori ammessi: $(decoder_list_str)
  RUNS                 numero di run su cui fare la media dei clock cycles
  PRIMES_FILE           file con un valore R per riga (path relativo alla cwd
                        o assoluto)
  NAME=VALUE ...       parametri di compile-time opzionali, passati come
                        -DCOMPILER_DEFINE_NAME=VALUE

Esempi:
  $(basename "$0") BFMAX 1000 _docs/primes/test_primes.txt ITER_MAX_BFMAX=200
  $(basename "$0") HYBRID 1000 _docs/primes/test_primes.txt \\
      ITER_MAX_HYBRID_BFMAX=200 ITER_MAX_HYBRID_BF=50
EOF
    exit 1
}

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------
if [[ $# -lt 3 ]]; then
    usage
fi

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    load_valid_decoders
    usage
fi

load_valid_decoders
DECODER="$(validate_decoder "$1")"
RUNS="$2"
PRIMES="$3"
shift 3

[[ "$RUNS" =~ ^[0-9]+$ ]] || die "RUNS deve essere un intero positivo, ricevuto '$RUNS'"
[[ -f "$PRIMES" ]] || die "file '$PRIMES' non esiste"

BUILD_FLAGS=(
  -DRELIABILITY_LEVEL=5
  -DUSE_AVX512=OFF
  -DBUILD_CC_TEST=ON
  -DCMAKE_BUILD_TYPE=Release
  -DSTANDALONE_IMPL=1
  -Wno-dev
)

# parametri opzionali NAME=VALUE
for param in "$@"; do
    if [[ ! "$param" =~ ^[A-Za-z_][A-Za-z0-9_]*=[^=]+$ ]]; then
        die "parametro non valido '$param'. Deve essere nel formato NAME=VALUE"
    fi
    BUILD_FLAGS+=("-DCOMPILER_DEFINE_$param")
done

# --------------------------------------------------------------------------
# Build and measure
# --------------------------------------------------------------------------
OUTPUT="$(make_output_dir cc)"
SRC_DIR="${ROOT_DIR}"
BUILD_DIR="${OUTPUT}/build"

info "start $(date +"%Y-%m-%d-%H-%M-%S")"
info "decoder: $DECODER, runs: $RUNS, primes: $PRIMES"

while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    rm -rf "$BUILD_DIR"
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
          "${BUILD_FLAGS[@]}" \
          -DDECODER="$DECODER" \
          -DR_BITS="$line"
    cmake --build "$BUILD_DIR" --parallel --target cc-test
    taskset -c "$CPU_CORE" "$BUILD_DIR/tests/cc-test" "$RUNS" | tee -a "$OUTPUT/cc.csv"
done < "$PRIMES"

sed -i '1!{/'"$RUNS"'/!d;}' "$OUTPUT/cc.csv" # keep only the numerical results

info "results saved to:"
echo "$OUTPUT/cc.csv"
info "end $(date +"%Y-%m-%d-%H-%M-%S")"
