#!/usr/bin/env bash

set -euo pipefail

readonly ESP_IDF_VERSION="5.5.5"
readonly ESP_IDF_COMMIT="b774170ff46c393eeb5e495ea37936038d3f4f4f"
readonly ESP_IDF_REPOSITORY="https://github.com/espressif/esp-idf.git"

if (($# != 1)); then
  echo "Usage: $0 <installation-directory>" >&2
  exit 2
fi

installation_directory="$1"

if [[ -e "${installation_directory}" && ! -d "${installation_directory}/.git" ]]; then
  echo "Refusing to replace non-ESP-IDF path: ${installation_directory}" >&2
  exit 2
fi

if [[ ! -d "${installation_directory}/.git" ]]; then
  git clone --branch "v${ESP_IDF_VERSION}" --depth 1 --recursive \
    "${ESP_IDF_REPOSITORY}" "${installation_directory}"
else
  git -C "${installation_directory}" submodule update --init --recursive
fi

actual_commit="$(git -C "${installation_directory}" rev-parse HEAD)"
if [[ "${actual_commit}" != "${ESP_IDF_COMMIT}" ]]; then
  echo "Expected ESP-IDF ${ESP_IDF_VERSION} at ${ESP_IDF_COMMIT}, got ${actual_commit}." >&2
  exit 2
fi

"${installation_directory}/install.sh" esp32s3
python3 "${installation_directory}/tools/idf_tools.py" install cmake ninja

echo "ESP-IDF ${ESP_IDF_VERSION} installed at ${installation_directory}."
echo "Activate it with: source ${installation_directory}/export.sh"
