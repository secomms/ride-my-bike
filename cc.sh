#!/bin/bash

# take as input:
# (1) decoder type
# (2) number of runs to average over
# (3) file listing all prime values to test, one per line
# (4) optional list of compile-time parameters (NAME=VALUE NAME=VALUE ...)
# for each prime, build the cryptosystem and measure the average clock cycles 
# taken for decapsulation, saving the results to a csv file

# examples:
# ./cc.sh BFMAX 1000 _docs/primes/test_primes.txt ITER_MAX_BFMAX=200
# ./cc.sh HYBRID 1000 _docs/primes/test_primes.txt ITER_MAX_HYBRID_BFMAX=200 ITER_MAX_HYBRID_BF=50

############################ default configuration #############################

set -e # exit on error

CPU_CORE=0 # pin the clock cycle measurement to this core

# output directory
OUTPUT="cc_$(date +"%Y-%m-%d-%H-%M-%S")"
mkdir -p "$OUTPUT"
echo "*" > "$OUTPUT/.gitignore"

# list of allowed decoders
ALLOWED_DECODERS=()
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    ALLOWED_DECODERS+=("$line")
done < "_docs/decoders.txt"

# fixed build flags
BUILD_FLAGS=(
  -DRELIABILITY_LEVEL=5
  -DUSE_AVX512=OFF
  -DBUILD_CC_TEST=ON
  -DCMAKE_BUILD_TYPE=Release # build for release, by default "-O3 -DNDEBUG"
  -DSTANDALONE_IMPL=1 # use internal AES, not OpenSSL
  -Wno-dev
)

############################### argument parsing ###############################

if [[ $# -lt 3 ]]; then
    echo "[!] Usage: $0 <decoder> <runs> <primes> [parameters]"
    exit 1
fi

DECODER="$1"
RUNS="$2"
PRIMES="$3"

# check for valid decoder
valid=false
for d in "${ALLOWED_DECODERS[@]}"; do
    if [[ "$DECODER" == "$d" ]]; then
        valid=true
        break
    fi
done
if [[ "$valid" == false ]]; then
    echo "[!] Invalid decoder '$DECODER'. Allowed values: ${ALLOWED_DECODERS[*]}"
    exit 1
fi

# check that the list of primes exists
if [[ ! -f "$PRIMES" ]]; then
    echo "[!] File '$PRIMES' does not exist"
    exit 1
fi

# parameters are passed as NAME=VALUE pairs, separated by spaces
for param in "${@:4}"; do
    # check that the parameter uses the correct format
    if [[ ! "$param" =~ ^[A-Za-z_][A-Za-z0-9_]*=[^=]+$ ]]; then
        echo "[!] Invalid parameter '$param'. Must be in the format NAME=VALUE"
        exit 1
    fi
    # add the parameter to the build definitions
    BUILD_FLAGS+=("-DCOMPILER_DEFINE_$param")
done

############################## build and measure ###############################

echo ">>>> start $(date +"%Y-%m-%d-%H-%M-%S")"

while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" =~ ^# ]] && continue
    rm -rf build
    mkdir build
    cd build
    cmake "${BUILD_FLAGS[@]}" -DDECODER="$DECODER" -DR_BITS="$line" ..
    cmake --build . --parallel --target cc-test
    cd ..
    taskset -c $CPU_CORE ./build/tests/cc-test $RUNS | tee -a "$OUTPUT/cc.csv"
done < "$PRIMES"
sed -i '1!{/'"$RUNS"'/!d;}' "$OUTPUT/cc.csv" # keep only the numerical results

echo ">>>> results saved to:"
echo "$OUTPUT/cc.csv"

echo ">>>> end $(date +"%Y-%m-%d-%H-%M-%S")"

################################################################################

# TODO: use -march=native instead of -mavx2 in cmake/compiler_flags.cmake
