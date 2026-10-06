.DEFAULT_GOAL := help
FIRMWARE_MANAGER_DIR ?= ../cardputer-firmware-manager
WORKSPACE ?= $(abspath ..)
SD ?= /Volumes/CARDPUTER
COMPANION_INSTALL_DIR ?= $(HOME)/Applications
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

# CRUB's shared extra slot from cardputer-firmware-manager layouts/cardputer-adv-8mb.csv;
# every CRUB application, including Hub, runs from it.
# make upload must not write Hub's standalone table; that hides hub_config at 0x7a0000.
CRUB_EXTRA_OFFSET := 0xd0000
CRUB_EXTRA_SIZE := 0x680000

.PHONY: setup lock-check architecture-check validate-idf validate-submodules configure build firmware-size test format format-check lint host-check firmware-check companion-check check upload upload-standalone migrate-storage-layout monitor clean
.PHONY: help build-idf firmware-size-idf flash stage doctor flash-usb upload-idf monitor-idf companion-build companion-run companion-install

help:
	@printf '%s\n' 'make build / check - build firmware / run all checks' 'make flash [SD=/Volumes/CARDPUTER] - build and stage on SD' 'make stage / doctor - stage existing image / validate mounted SD' 'make flash-usb UPLOAD_PORT=/dev/cu... - replace CRUB extra slot over USB' 'make monitor [UPLOAD_PORT=/dev/cu...] - serial monitor' 'make companion-build / companion-run / companion-install / companion-check'

flash stage:
	$(MAKE) -C "$(FIRMWARE_MANAGER_DIR)" $@ APP=hub WORKSPACE="$(WORKSPACE)" SD="$(SD)"

doctor:
	$(MAKE) -C "$(FIRMWARE_MANAGER_DIR)" doctor-sd SD="$(SD)"

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
	@test -f components/m5utility/upstream/src/M5Utility.hpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)
	@test -f components/m5hal/upstream/src/M5HAL.hpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)
	@test -f components/m5unitunified/upstream/src/M5UnitUnified.hpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)
	@test -f components/m5unitnfc/upstream/src/M5UnitUnifiedNFC.hpp || (echo "Run: git submodule update --init --recursive" >&2; exit 2)

configure: validate-idf validate-submodules
	$(IDF_RUN) $(IDF_ARGS) reconfigure

build:
	bash scripts/build_firmware.sh

build-idf: validate-idf validate-submodules
	$(IDF_RUN) $(IDF_ARGS) build
	@test -f $(IDF_APP_IMAGE)
	@test -f $(IDF_PARTITION_IMAGE)

firmware-size:
	bash scripts/build_firmware.sh firmware-size-idf

firmware-size-idf: validate-idf
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
	cd companion/macos && swift run CompanionProvidersCheck
	bash scripts/package_macos_companion.sh

companion-build:
	bash scripts/package_macos_companion.sh

companion-run: companion-build
	open "companion/macos/Cardputer Companion.app"

companion-install: companion-build
	python3 scripts/install_companion.py "companion/macos/Cardputer Companion.app" "$(COMPANION_INSTALL_DIR)"

firmware-check: build
	python3 scripts/check_esp_idf_config.py "$(IDF_CONFIG_HEADER)"

check: host-check firmware-check

flash-usb: upload

upload:
	@test -n "$(UPLOAD_PORT)" || (echo 'UPLOAD_PORT is required.' >&2; exit 2)
	bash scripts/build_firmware.sh upload-idf

upload-idf: validate-idf validate-submodules
	@test -n "$(UPLOAD_PORT)" || (echo "UPLOAD_PORT is required. make upload writes only CRUB's shared extra slot at $(CRUB_EXTRA_OFFSET) and does not replace the shared partition table." >&2; exit 2)
	$(MAKE) build-idf
	@size=$$(wc -c < "$(IDF_APP_IMAGE)" | tr -d ' '); test "$$size" -le $$(($(CRUB_EXTRA_SIZE))) || (echo "$(IDF_APP_IMAGE) is $$size bytes; the CRUB extra slot holds $$(($(CRUB_EXTRA_SIZE))) bytes." >&2; exit 2)
	esptool.py --chip esp32s3 --port "$(UPLOAD_PORT)" -b 1500000 --before default_reset --after hard_reset write_flash $(CRUB_EXTRA_OFFSET) $(IDF_APP_IMAGE)

upload-standalone: validate-idf validate-submodules
	@echo "warning: upload-standalone writes Hub's partition table and remaps hub_config to 0x7e0000; do not use it on a CRUB device." >&2
	$(IDF_RUN) $(IDF_ARGS) -b 1500000 $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) flash

migrate-storage-layout:
	@test -n "$(UPLOAD_PORT)" || (echo "UPLOAD_PORT is required for storage-layout migration." >&2; exit 2)
	$(MAKE) upload-standalone UPLOAD_PORT="$(UPLOAD_PORT)"
	esptool.py --chip esp32s3 --port "$(UPLOAD_PORT)" erase_region 0x7e0000 0x10000

monitor:
	bash scripts/build_firmware.sh monitor-idf

monitor-idf: validate-idf
	$(IDF_RUN) $(IDF_ARGS) $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) monitor

clean: validate-idf
	$(IDF_RUN) $(IDF_ARGS) fullclean
