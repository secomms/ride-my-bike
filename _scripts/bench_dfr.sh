#!/usr/bin/env bash
# exit on error
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common/common.sh"

TOTAL_CORES=$(nproc)
DEFAULT_FREE_CORES=1
DEFAULT_CORES=$(( TOTAL_CORES - DEFAULT_FREE_CORES ))
(( DEFAULT_CORES < 1 )) && DEFAULT_CORES=1
DEFAULT_OFFSET=0
DEFAULT_CMAKE_JOBS=1

usage() {
    cat >&2 <<EOF
Usage: $(basename "$0") <DECODER> [-c CORES] [-o OFFSET] [-p PARALLEL_BUILDS] [-j CMAKE_JOBS]

  DECODER              decoder da usare per tutte le build (case-insensitive)
                        valori ammessi: $(decoder_list_str)

  -c CORES            numero di core che questa istanza puo' usare per il
                       run finale con gnu parallel
                       (default: $DEFAULT_CORES, cioe' nproc-$DEFAULT_FREE_CORES)
  -o OFFSET           id del primo core da cui partire (default $DEFAULT_OFFSET).
                       Usalo per lanciare piu' istanze dello script in
                       parallelo senza che si sovrappongano sugli stessi
                       core, es: istanza1 -c 8 -o 0, istanza2 -c 8 -o 8
  -p PARALLEL_BUILDS  quante build cmake in parallelo nella fase di build
                       (default: min(CORES,4))
  -j CMAKE_JOBS       job interni a ogni singola build cmake (default $DEFAULT_CMAKE_JOBS)

Esempi:
  $(basename "$0") BGF
  $(basename "$0") BFMAX -c 16 -o 0
EOF
    exit 1
}

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------
if [[ $# -lt 1 ]]; then
    usage
fi

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    load_valid_decoders
    usage
fi

load_valid_decoders
DECODER="$(validate_decoder "$1")"
shift

CORES="$DEFAULT_CORES"
OFFSET="$DEFAULT_OFFSET"
PARALLEL_BUILDS=""
CMAKE_JOBS="$DEFAULT_CMAKE_JOBS"

while getopts "c:o:p:j:h" opt; do
    case "$opt" in
        c) CORES="$OPTARG" ;;
        o) OFFSET="$OPTARG" ;;
        p) PARALLEL_BUILDS="$OPTARG" ;;
        j) CMAKE_JOBS="$OPTARG" ;;
        h) usage ;;
        *) usage ;;
    esac
done

if (( OFFSET + CORES > TOTAL_CORES )); then
    die "OFFSET ($OFFSET) + CORES ($CORES) supera i core disponibili ($TOTAL_CORES)"
fi

if [ -z "$PARALLEL_BUILDS" ]; then
    PARALLEL_BUILDS=$(( CORES < 4 ? CORES : 4 ))
fi

# --------------------------------------------------------------------------
# Config
# --------------------------------------------------------------------------
SRC_DIR="${ROOT_DIR}"
RVALUES="${SCRIPT_DIR}/common/primes/L1_primes.txt"
[[ -f "${RVALUES}" ]] || die "file dei primi non trovato: ${RVALUES}"
TESTS_DFR=10000

OUTPUT="$(make_output_dir dfr)"
BUILD_ROOT="$OUTPUT/builds"
mkdir -p "$BUILD_ROOT"

################################################################################
info "configuring + building (parallel, one build dir per R)"
info "decoder: $DECODER"
info "using $CORES cores starting at offset $OFFSET (of $TOTAL_CORES total)"

build_one() {
    local r="$1"
    local build_dir="$BUILD_ROOT/R-$r"
    local log="$BUILD_ROOT/R-$r.log"
    cmake -S "$SRC_DIR" -B "$build_dir" \
          -DCMAKE_BUILD_TYPE=Release \
          -DSTANDALONE_IMPL=1 \
          -DRDTSC=1 \
          -DRELIABILITY_LEVEL=5 \
          -DCMAKE_POLICY_VERSION_MINIMUM=3.11 \
          -DUSE_AVX512=OFF \
          -DDECODER="$DECODER" \
          -DR_BITS="$r" \
          -DBUILD_DFR_TEST=ON \
          -Wno-dev \
          > "$log" 2>&1
    cmake --build "$build_dir" --target bike-dfr-test -j "$CMAKE_JOBS" \
          >> "$log" 2>&1
    cp "$build_dir/tests/bike-dfr-test" "$OUTPUT/R-$r.out"
}
export -f build_one
export SRC_DIR BUILD_ROOT OUTPUT CMAKE_JOBS DECODER

i=0
while IFS= read -r r || [ -n "$r" ]; do
    i=$((i + 1))
    build_one "$r" &
    if (( i % PARALLEL_BUILDS == 0 )); then
        wait
    fi
done < "$RVALUES"
wait

################################################################################
info "measuring decoding failure rate (parallel)"

find "$OUTPUT" -maxdepth 1 -type f -executable | \
   parallel -j "$CORES" --joblog "$OUTPUT/log.txt" \
   "taskset -c \$(( $OFFSET + (({%} - 1) % $CORES) )) {}" $TESTS_DFR >> "$OUTPUT/dfr.csv"

sort -t, -k2,2n "$OUTPUT/dfr.csv" -o "$OUTPUT/dfr.csv"

################################################################################
info "done"
echo "$OUTPUT/dfr.csv"
