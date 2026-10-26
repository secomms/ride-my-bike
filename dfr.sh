#!/bin/bash
# exit on error
set -e

# --- args -----------------------------------------------------------------
# --- default -----------------------------------------------------------
TOTAL_CORES=$(nproc)
DEFAULT_FREE_CORES=1   # core lasciati liberi per il sistema se -c non e' passato
DEFAULT_CORES=$(( TOTAL_CORES - DEFAULT_FREE_CORES ))
(( DEFAULT_CORES < 1 )) && DEFAULT_CORES=1
DEFAULT_OFFSET=0
DEFAULT_CMAKE_JOBS=1

usage() {
    cat >&2 <<EOF
Usage: $0 [-c CORES] [-o OFFSET] [-p PARALLEL_BUILDS] [-j CMAKE_JOBS]

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
EOF
    exit 1
}

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
    echo "Errore: OFFSET ($OFFSET) + CORES ($CORES) supera i core disponibili ($TOTAL_CORES)" >&2
    exit 1
fi

# se non specificato, usa al massimo 4 build parallele o CORES se minore
if [ -z "$PARALLEL_BUILDS" ]; then
    PARALLEL_BUILDS=$(( CORES < 4 ? CORES : 4 ))
fi

# --- config -----------------------------------------------------------
# inputs are in a text file, one R value per line
RVALUES="_docs/primes/L1_primes.txt"
NRVALUES=$(wc -l < "$RVALUES")
# path to the top-level CMakeLists.txt (adjust if this script lives elsewhere)
SRC_DIR="$(pwd)"
TESTS_DFR=10000
# (TOTAL_CORES, CORES, OFFSET gia' calcolati/parsati sopra)

# output directory
OUTPUT=$(date +"%Y-%m-%d-%H-%M-%S")
mkdir -p "$OUTPUT"
echo "*" > "$OUTPUT/.gitignore"
BUILD_ROOT="$OUTPUT/builds"
mkdir -p "$BUILD_ROOT"

################################################################################
echo ">>>> configuring + building (parallel, one build dir per R)"
echo ">>>> using $CORES cores starting at offset $OFFSET (of $TOTAL_CORES total)"

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
          -DDECODER=BFMAX \
          -DR_BITS="$r" \
          -DBUILD_DFR_TEST=ON \
          -Wno-dev \
          > "$log" 2>&1
    cmake --build "$build_dir" --target bike-dfr-test -j "$CMAKE_JOBS" \
          >> "$log" 2>&1
    cp "$build_dir/tests/bike-dfr-test" "$OUTPUT/R-$r.out"
}
export -f build_one
export SRC_DIR BUILD_ROOT OUTPUT CMAKE_JOBS

# run builds with bounded parallelism (avoids spawning NRVALUES cmake
# configure processes at once, which can thrash the filesystem/CPU)
i=0
while IFS= read -r r || [ -n "$r" ]; do
    i=$((i + 1))
    build_one "$r" &
    # throttle: wait once we've launched PARALLEL_BUILDS jobs
    if (( i % PARALLEL_BUILDS == 0 )); then
        wait
    fi
done < "$RVALUES"
wait

################################################################################
echo ">>>> measuring decoding failure rate (parallel)"

find "$OUTPUT" -maxdepth 1 -type f -executable | \
   parallel -j "$CORES" --joblog "$OUTPUT/log.txt" \
   "taskset -c \$(( $OFFSET + (({%} - 1) % $CORES) )) {}" $TESTS_DFR >> "$OUTPUT/dfr.csv"

sort -t, -k2,2n "$OUTPUT/dfr.csv" -o "$OUTPUT/dfr.csv"

################################################################################
# keep the .out binaries this time if you want to re-run without rebuilding;
# uncomment to clean up as in the original script
# find "$OUTPUT" -maxdepth 1 -type f -executable -delete
echo ">>>> done"
