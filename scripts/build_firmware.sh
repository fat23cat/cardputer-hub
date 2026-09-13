#!/usr/bin/env bash

set -euo pipefail

readonly hub_script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly hub_project="$(cd "${hub_script_directory}/.." && pwd)"
readonly hub_default_idf_path="${HOME}/.espressif/frameworks/esp-idf-v5.5.5"
readonly hub_local_idf_path="${hub_project}/build-tools/esp-idf-v5.5.5"
readonly hub_local_idf_tools_path="${hub_project}/build-tools/idf-tools-v5.5.5"

if [[ -z "${CARDPUTER_HUB_VERSION:-}" ]]; then
  hub_base_version="$(git -C "${hub_project}" describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null || true)"
  hub_base_version="${hub_base_version#v}"
  if [[ -z "${hub_base_version}" ]]; then
    hub_base_version="0.1.0-dev"
  fi
  export CARDPUTER_HUB_VERSION="${hub_base_version}+$(date '+%Y%m%d-%H%M')"
fi

if [[ -n "${CARDPUTER_HUB_IDF_PATH:-}" ]]; then
  hub_idf_path="${CARDPUTER_HUB_IDF_PATH}"
elif [[ -f "${hub_default_idf_path}/export.sh" ]]; then
  hub_idf_path="${hub_default_idf_path}"
elif [[ -f "${hub_local_idf_path}/export.sh" ]]; then
  hub_idf_path="${hub_local_idf_path}"
else
  hub_idf_path="${hub_default_idf_path}"
fi
readonly hub_idf_path

if [[ ! -f "${hub_idf_path}/export.sh" ]]; then
  echo "ESP-IDF 5.5.5 is not installed at ${hub_idf_path}." >&2
  echo "Run: bash scripts/install_esp_idf.sh ${hub_default_idf_path}" >&2
  exit 2
fi

export IDF_PATH="${hub_idf_path}"
if [[ -n "${CARDPUTER_HUB_IDF_TOOLS_PATH:-}" ]]; then
  export IDF_TOOLS_PATH="${CARDPUTER_HUB_IDF_TOOLS_PATH}"
elif [[ "${hub_idf_path}" == "${hub_local_idf_path}" && -d "${hub_local_idf_tools_path}" ]]; then
  export IDF_TOOLS_PATH="${hub_local_idf_tools_path}"
else
  unset IDF_TOOLS_PATH
fi

# shellcheck disable=SC1091
source "${hub_idf_path}/export.sh"
exec make -C "${hub_project}" build
