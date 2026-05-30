#!/usr/bin/env bash
# fetch_hrtf_datasets.sh — Download HRTF datasets from catalog and convert to .speh
# Usage:
#   ./scripts/fetch_hrtf_datasets.sh [--dry-run] [--accept-license=<name>]
#
# --dry-run          : Perform HTTP HEAD checks only; no downloads.
# --accept-license=X : Allow fetching the named non-CC-BY entry (can repeat).
#
# Auto-fetch policy: only entries with auto_fetch=true AND license starting
# with "CC-BY" are fetched by default. Others require --accept-license=<name>.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CATALOG="${REPO_ROOT}/assets/hrtf/catalog.json"
RAW_DIR="${REPO_ROOT}/assets/hrtf/raw"
SOFA_TO_BIN="${REPO_ROOT}/tools/sofa_to_bin.py"

DRY_RUN=false
declare -a ACCEPT_LICENSES=()

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
for arg in "$@"; do
  case "${arg}" in
    --dry-run)
      DRY_RUN=true
      ;;
    --accept-license=*)
      ACCEPT_LICENSES+=("${arg#--accept-license=}")
      ;;
    *)
      echo "ERROR: Unknown argument: ${arg}" >&2
      echo "Usage: $0 [--dry-run] [--accept-license=<name>]" >&2
      exit 1
      ;;
  esac
done

# ---------------------------------------------------------------------------
# Read catalog via python3 (jq may not be installed)
# ---------------------------------------------------------------------------
CATALOG_DATA="$(python3 -c "
import json, sys
with open('${CATALOG}') as f:
    data = json.load(f)
for entry in data['hrtf_catalog']:
    print('\t'.join([
        entry['name'],
        entry['sofa_source_url'],
        entry['license'],
        entry['speh_path'],
        str(entry.get('auto_fetch', False)),
    ]))
")"

if [[ -z "${CATALOG_DATA}" ]]; then
  echo "ERROR: Failed to parse catalog or catalog is empty." >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Helper: check if name is in --accept-license list
# ---------------------------------------------------------------------------
is_accepted() {
  local name="$1"
  for accepted in "${ACCEPT_LICENSES[@]+"${ACCEPT_LICENSES[@]}"}"; do
    if [[ "${accepted}" == "${name}" ]]; then
      return 0
    fi
  done
  return 1
}

# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
if "${DRY_RUN}"; then
  echo "=== DRY-RUN mode: HTTP HEAD checks only (no downloads) ==="
  echo ""
fi

if ! "${DRY_RUN}"; then
  mkdir -p "${RAW_DIR}"
fi

overall_ok=true

while IFS=$'\t' read -r name url license speh_path auto_fetch; do
  cc_by=false
  if [[ "${license}" == CC-BY* ]]; then
    cc_by=true
  fi

  if "${DRY_RUN}"; then
    # HEAD check — do NOT abort on curl error; report status per URL
    echo -n "[${name}] HEAD ${url} ... "
    http_status="$(curl -fsSI --max-time 15 --retry 2 -o /dev/null -w '%{http_code}' "${url}" 2>/dev/null || true)"
    if [[ -z "${http_status}" || "${http_status}" == "000" ]]; then
      echo "UNREACHABLE (no response or connection error)"
    else
      echo "HTTP ${http_status}"
    fi
    continue
  fi

  # --- Live fetch ---
  # Determine whether to fetch
  should_fetch=false
  if "${auto_fetch}" && "${cc_by}"; then
    should_fetch=true
  elif is_accepted "${name}"; then
    should_fetch=true
  fi

  if ! "${should_fetch}"; then
    if "${cc_by}"; then
      echo "[${name}] SKIPPED: auto_fetch=false (pass --accept-license=${name} to fetch)"
    else
      echo "[${name}] SKIPPED (non-CC-BY license: ${license})."
      echo "         Manual consent required. Re-run with --accept-license=${name} to fetch."
      echo "         Ensure you comply with the license terms before using this dataset."
    fi
    continue
  fi

  sofa_filename="${name}.sofa"
  sofa_dest="${RAW_DIR}/${sofa_filename}"

  echo "[${name}] Downloading ${url} ..."
  if curl -fL --retry 3 --max-time 300 -o "${sofa_dest}" "${url}"; then
    echo "[${name}] Download complete: ${sofa_dest}"
  else
    echo "[${name}] ERROR: Download failed for ${url}" >&2
    overall_ok=false
    continue
  fi

  speh_abs="${REPO_ROOT}/${speh_path}"
  echo "[${name}] Converting to .speh: ${speh_abs}"
  if python3 "${SOFA_TO_BIN}" "${sofa_dest}" "${speh_abs}"; then
    echo "[${name}] Conversion complete: ${speh_abs}"
  else
    echo "[${name}] ERROR: sofa_to_bin.py failed for ${sofa_dest}" >&2
    overall_ok=false
  fi

done <<< "${CATALOG_DATA}"

if "${DRY_RUN}"; then
  echo ""
  echo "=== DRY-RUN complete. No files downloaded. ==="
  exit 0
fi

if "${overall_ok}"; then
  echo ""
  echo "=== fetch_hrtf_datasets.sh complete. ==="
else
  echo ""
  echo "=== fetch_hrtf_datasets.sh completed with errors. ===" >&2
  exit 1
fi
