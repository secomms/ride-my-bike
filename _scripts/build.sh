#!/usr/bin/env bash
#
# Wrapper for a clean BIKE build, with the decoder selectable from the
# command line. The project's "fixed" flags are hardcoded below; edit
# FIXED_CMAKE_FLAGS if you need to change them in the future.
#
# Usage:
#   ./_scripts/build.sh <DECODER> [options]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common/common.sh"

SOURCE_DIR="${ROOT_DIR}"
BUILD_DIR_NAME="build"
USE_AVX512="OFF"
JOBS="$(nproc 2>/dev/null || echo 4)"
R_BITS=""
MARCH=""
MARCH_SET_BY_USER=false

FIXED_CMAKE_FLAGS=(
  -DCMAKE_BUILD_TYPE=Release
  -DSTANDALONE_IMPL=1
  -DRDTSC=1
  -DRELIABILITY_LEVEL=5
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  -Wno-dev
)

usage() {
  cat <<EOF
Usage: $(basename "$0") <DECODER> [options]

Builds the project rooted at:
  ${ROOT_DIR}

DECODER (required, case-insensitive):
  $(decoder_list_str)

Options:
  --avx512               Enable USE_AVX512=ON (default: OFF, AVX2 only)
  --build-dir <dir>      Build directory (default: "build" at project root).
                          Relative paths are resolved against the project root.
  --jobs <N>              Number of parallel make jobs (default: nproc)
  --r-bits <N>            Override R_BITS (default: unset, uses project default)
  --march <target>        Manual override for -march (default: automatic, see below)
  -h, --help              Show this help

Default -march target (if --march is not passed):
  --avx512 NOT passed -> -march=x86-64-v3
  --avx512 passed      -> -march=native

Examples:
  $(basename "$0") BGF
  $(basename "$0") BFMAX --avx512
  $(basename "$0") OOP --build-dir build-oop --jobs 8
  $(basename "$0") BGF --r-bits 12323
  $(basename "$0") BFMAX --avx512 --march=skylake-avx512
EOF
}

resolve_build_dir() {
  local dir="$1"
  if [[ "${dir}" = /* ]]; then
    echo "${dir}"
  else
    echo "${ROOT_DIR}/${dir}"
  fi
}

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------
if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  load_valid_decoders
  usage
  exit 0
fi

load_valid_decoders
DECODER="$(validate_decoder "$1")"
shift

while [[ $# -gt 0 ]]; do
  case "$1" in
    --avx512)      USE_AVX512="ON"; shift ;;
    --build-dir)   [[ $# -ge 2 ]] || die "--build-dir requires an argument"
                   BUILD_DIR_NAME="$2"; shift 2 ;;
    --jobs)        [[ $# -ge 2 ]] || die "--jobs requires an argument"
                   JOBS="$2"; shift 2 ;;
    --r-bits)      [[ $# -ge 2 ]] || die "--r-bits requires an argument"
                   R_BITS="$2"; shift 2 ;;
    --march)       [[ $# -ge 2 ]] || die "--march requires an argument"
                   MARCH="$2"; MARCH_SET_BY_USER=true; shift 2 ;;
    -h|--help)     usage; exit 0 ;;
    *)             die "unknown option: $1" ;;
  esac
done

BUILD_DIR="$(resolve_build_dir "${BUILD_DIR_NAME}")"

if [[ "${MARCH_SET_BY_USER}" == false ]]; then
  if [[ "${USE_AVX512}" == "ON" ]]; then
    MARCH="native"
  else
    MARCH="x86-64-v3"
  fi
fi

CMAKE_FLAGS=(
  "${FIXED_CMAKE_FLAGS[@]}"
  -DDECODER="${DECODER}"
  -DUSE_AVX512="${USE_AVX512}"
  -DCMAKE_C_FLAGS="-march=${MARCH}"
  -DCMAKE_CXX_FLAGS="-march=${MARCH}"
)
[[ -n "${R_BITS}" ]] && CMAKE_FLAGS+=(-DR_BITS="${R_BITS}")

# --------------------------------------------------------------------------
# Clean build (quiet output: final result only)
# --------------------------------------------------------------------------
CONFIG_LOG="$(mktemp)"
BUILD_LOG="$(mktemp)"
trap 'rm -f "${CONFIG_LOG}" "${BUILD_LOG}"' EXIT

START_TIME=$(date +%s)

hr
echo -e "${BOLD}${CYAN}BIKE build${RESET}"
hr
printf "  %-14s %s\n" "Project root:" "${ROOT_DIR}"
printf "  %-14s %s\n" "Decoder:"    "${DECODER}"
printf "  %-14s %s\n" "USE_AVX512:" "${USE_AVX512}"
printf "  %-14s %s\n" "-march:"     "${MARCH}"
printf "  %-14s %s\n" "Build dir:"  "${BUILD_DIR}"
printf "  %-14s %s\n" "Jobs:"       "${JOBS}"
[[ -n "${R_BITS}" ]] && printf "  %-14s %s\n" "R_BITS:" "${R_BITS}"
hr

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

step_start "CMake configuration"
if cmake -S "${SOURCE_DIR}" -B "${BUILD_DIR}" "${CMAKE_FLAGS[@]}" > "${CONFIG_LOG}" 2>&1; then
  step_ok
else
  step_fail
  echo
  echo -e "${YELLOW}${BOLD}── Configuration log ──${RESET}"
  cat "${CONFIG_LOG}"
  hr
  die "Build failed (CMake configuration error)"
fi

step_start "Compilation"
if cmake --build "${BUILD_DIR}" -- -j"${JOBS}" > "${BUILD_LOG}" 2>&1; then
  step_ok
else
  step_fail
  echo
  echo -e "${YELLOW}${BOLD}── Build log ──${RESET}"
  cat "${BUILD_LOG}"
  hr
  die "Build failed (compilation error)"
fi

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

hr
echo -e "${CHECK} ${BOLD}${GREEN}Build completed successfully${RESET} ${DIM}(${ELAPSED}s)${RESET}"
printf "  %-14s %s\n" "Decoder:"    "${DECODER}"
printf "  %-14s %s\n" "USE_AVX512:" "${USE_AVX512}"
printf "  %-14s %s\n" "-march:"     "${MARCH}"
[[ -n "${R_BITS}" ]] && printf "  %-14s %s\n" "R_BITS:" "${R_BITS}"
printf "  %-14s %s\n" "Output:" "${BUILD_DIR}/"
hr
