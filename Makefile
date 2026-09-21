UV ?= uv
RUN := $(UV) run --frozen
IDF_PY ?= idf.py
IDF_BUILD_DIR ?= build
IDF_RUN := $(IDF_PY)
IDF_ARGS := -B $(IDF_BUILD_DIR) -D IDF_TARGET=esp32s3 -D CARDPUTER_HUB_VERSION_OVERRIDE=$(CARDPUTER_HUB_VERSION)
IDF_APP_IMAGE := $(IDF_BUILD_DIR)/cardputer_hub.bin
IDF_PARTITION_IMAGE := $(IDF_BUILD_DIR)/partition_table/partition-table.bin
IDF_CONFIG_HEADER := $(IDF_BUILD_DIR)/config/sdkconfig.h
CPP_FILES := $(shell find src test -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) | sort)

# CRUB hub partition from cardputer-firmware-manager layouts/cardputer-adv-8mb.csv.
# make upload must not write Hub's standalone table; that hides hub_config at 0x560000.
CRUB_HUB_OFFSET := 0xd0000

.PHONY: setup lock-check architecture-check validate-idf validate-submodules configure build firmware-size test format format-check lint host-check firmware-check companion-check check upload upload-standalone migrate-storage-layout monitor clean

setup: validate-idf
	$(UV) sync --frozen
	git submodule update --init --recursive
	$(IDF_RUN) $(IDF_ARGS) reconfigure

lock-check:
	$(UV) lock --check

architecture-check:
	$(RUN) python scripts/check_architecture.py

validate-idf:
	@command -v $(IDF_PY) >/dev/null || (echo "ESP-IDF 5.5.5 is not active; source its export.sh first." >&2; exit 2)
	@$(IDF_RUN) --version | grep -Fx "ESP-IDF v5.5.5" >/dev/null || (echo "Cardputer Hub requires exactly ESP-IDF 5.5.5." >&2; exit 2)

validate-submodules:
	@test -f components/m5cardputer/upstream/src/M5Cardputer.cpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)
	@test -f components/arduino_irremote/upstream/src/IRremote.hpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)

configure: validate-idf validate-submodules
	$(IDF_RUN) $(IDF_ARGS) reconfigure

build: validate-idf validate-submodules
	$(IDF_RUN) $(IDF_ARGS) build
	@test -f $(IDF_APP_IMAGE)
	@test -f $(IDF_PARTITION_IMAGE)

firmware-size: validate-idf
	@test -f $(IDF_APP_IMAGE) || (echo "Run 'make build' before 'make firmware-size'." >&2; exit 2)
	@bash -o pipefail -c '$(IDF_RUN) $(IDF_ARGS) size | tee "$(IDF_BUILD_DIR)/firmware-size.txt"'
	@bash -o pipefail -c '$(IDF_RUN) $(IDF_ARGS) size-components | tee "$(IDF_BUILD_DIR)/firmware-size-components.txt"'

test:
	$(RUN) python -m unittest discover -s test_python
	$(RUN) pio test -e native

format:
	$(RUN) clang-format -i $(CPP_FILES)

format-check:
	$(RUN) clang-format --dry-run --Werror $(CPP_FILES)

lint:
	$(RUN) pio check -e native

host-check: lock-check architecture-check format-check lint test

companion-check:
	@test "$$(uname)" = Darwin || (echo "companion-check requires macOS." >&2; exit 2)
	cd companion/macos && swift run CompanionCoreCheck
	bash scripts/package_macos_companion.sh

firmware-check: build
	python3 scripts/check_esp_idf_config.py "$(IDF_CONFIG_HEADER)"

check: host-check firmware-check

upload: validate-idf validate-submodules
	@test -n "$(UPLOAD_PORT)" || (echo "UPLOAD_PORT is required. make upload writes only the CRUB hub partition at $(CRUB_HUB_OFFSET) and does not replace the shared partition table." >&2; exit 2)
	$(MAKE) build
	esptool.py --chip esp32s3 --port "$(UPLOAD_PORT)" -b 1500000 --before default_reset --after hard_reset write_flash $(CRUB_HUB_OFFSET) $(IDF_APP_IMAGE)

upload-standalone: validate-idf validate-submodules
	@echo "warning: upload-standalone writes Hub's partition table and remaps hub_config to 0x7e0000; do not use it on a CRUB device." >&2
	$(IDF_RUN) $(IDF_ARGS) -b 1500000 $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) flash

migrate-storage-layout:
	@test -n "$(UPLOAD_PORT)" || (echo "UPLOAD_PORT is required for storage-layout migration." >&2; exit 2)
	$(MAKE) upload-standalone UPLOAD_PORT="$(UPLOAD_PORT)"
	esptool.py --chip esp32s3 --port "$(UPLOAD_PORT)" erase_region 0x7e0000 0x10000

monitor: validate-idf
	$(IDF_RUN) $(IDF_ARGS) $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) monitor

clean: validate-idf
	$(IDF_RUN) $(IDF_ARGS) fullclean
