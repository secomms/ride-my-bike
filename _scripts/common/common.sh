#!/usr/bin/env bash
#
# Funzioni condivise tra gli script in _scripts/. Questo file va SOURCEATO,
# non eseguito direttamente:
#   source "${SCRIPT_DIR}/common.sh"
#
set -euo pipefail

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  echo "common.sh è una libreria, va sourceata (source common.sh), non eseguita." >&2
  exit 1
fi

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------
# COMMON_DIR = cartella in cui vive questo file (_scripts/)
COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${COMMON_DIR}/../.." && pwd)"
DECODERS_FILE="${COMMON_DIR}/decoders.txt"

# --------------------------------------------------------------------------
# Colori e simboli (disattivati automaticamente se non su un terminale)
# --------------------------------------------------------------------------
if [[ -t 1 ]]; then
  BOLD="\033[1m"; DIM="\033[2m"; RED="\033[31m"; GREEN="\033[32m"
  YELLOW="\033[33m"; CYAN="\033[36m"; RESET="\033[0m"
else
  BOLD=""; DIM=""; RED=""; GREEN=""; YELLOW=""; CYAN=""; RESET=""
fi
CHECK="${GREEN}✓${RESET}"
CROSS="${RED}✗${RESET}"

hr() { printf '%s\n' "――――――――――――――――――――――――――――――――――――――――――――――――――――――"; }
die() { echo -e "${CROSS} ${RED}Error:${RESET} $*" >&2; exit 1; }
info() { echo -e "${CYAN}>>>>${RESET} $*"; }

step_start() { printf "  %-28s" "$1"; }
step_ok()    { echo -e "${CHECK} ${GREEN}OK${RESET}"; }
step_fail()  { echo -e "${CROSS} ${RED}FAILED${RESET}"; }

# --------------------------------------------------------------------------
# Decoder
# --------------------------------------------------------------------------
VALID_DECODERS=()

# Carica i decoder ammessi da _scripts/decoders.txt (uno per riga,
# righe vuote e righe che iniziano con # vengono ignorate).
load_valid_decoders() {
  [[ -f "${DECODERS_FILE}" ]] || die "decoders file non trovato: ${DECODERS_FILE}"
  VALID_DECODERS=()
  while IFS= read -r line || [[ -n "${line}" ]]; do
    line="$(echo "${line}" | tr -d '[:space:]')"
    [[ -z "${line}" ]] && continue
    [[ "${line}" == \#* ]] && continue
    VALID_DECODERS+=("$(echo "${line}" | tr '[:lower:]' '[:upper:]')")
  done < "${DECODERS_FILE}"
  [[ ${#VALID_DECODERS[@]} -gt 0 ]] || die "decoders file vuoto: ${DECODERS_FILE}"
}

# validate_decoder <raw> -> stampa il nome del decoder validato/maiuscolato,
# oppure termina lo script con die() se non e' tra quelli ammessi.
# Uso: DECODER="$(validate_decoder "$1")"
validate_decoder() {
  local raw="$1" upper d
  upper="$(echo "${raw}" | tr '[:lower:]' '[:upper:]')"
  for d in "${VALID_DECODERS[@]}"; do
    if [[ "${upper}" == "${d}" ]]; then
      echo "${upper}"
      return 0
    fi
  done
  die "decoder non valido '${raw}'. Valori ammessi: ${VALID_DECODERS[*]}"
}

# decoder_list_str -> stringa "BGF | BFMAX | ..." per gli usage(), usa un
# fallback se load_valid_decoders() non e' ancora stata chiamata.
decoder_list_str() {
  if [[ ${#VALID_DECODERS[@]} -gt 0 ]]; then
    (IFS=' | '; echo "${VALID_DECODERS[*]}")
  else
    echo "(vedi ${DECODERS_FILE})"
  fi
}

# --------------------------------------------------------------------------
# Output directories
# --------------------------------------------------------------------------
# make_output_dir <prefix> -> crea <ROOT_DIR>/<prefix>_<timestamp>/ con un
# .gitignore che ignora tutto il contenuto, e stampa il path creato.
# Uso: OUTPUT="$(make_output_dir cc)"
make_output_dir() {
  local prefix="$1"
  local dir="${ROOT_DIR}/${prefix}_$(date +"%Y-%m-%d-%H-%M-%S")"
  mkdir -p "${dir}"
  echo "*" > "${dir}/.gitignore"
  echo "${dir}"
}
