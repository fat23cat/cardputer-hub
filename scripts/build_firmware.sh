#!/usr/bin/env bash

set -euo pipefail

readonly hub_script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly hub_project="$(cd "${hub_script_directory}/.." && pwd)"
readonly hub_default_idf_path="${HOME}/.espressif/frameworks/esp-idf-v5.5.5"
readonly hub_local_idf_path="${hub_project}/build-tools/esp-idf-v5.5.5"
readonly hub_local_idf_tools_path="${hub_project}/build-tools/idf-tools-v5.5.5"

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
