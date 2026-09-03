UV ?= uv
RUN := $(UV) run --frozen
IDF_PY ?= idf.py
IDF_BUILD_DIR ?= build
IDF_RUN := $(IDF_PY)
IDF_ARGS := -B $(IDF_BUILD_DIR) -D IDF_TARGET=esp32s3
IDF_APP_IMAGE := $(IDF_BUILD_DIR)/cardputer_hub.bin
IDF_PARTITION_IMAGE := $(IDF_BUILD_DIR)/partition_table/partition-table.bin
CPP_FILES := $(shell find src test -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) | sort)

.PHONY: setup lock-check validate-idf validate-submodules configure build test format format-check lint check upload migrate-storage-layout monitor clean

setup: validate-idf
	$(UV) sync --frozen
	git submodule update --init --recursive
	$(IDF_RUN) $(IDF_ARGS) reconfigure

lock-check:
	$(UV) lock --check

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

test:
	$(RUN) python -m unittest discover -s test_python
	$(RUN) pio test -e native

format:
	$(RUN) clang-format -i $(CPP_FILES)

format-check:
	$(RUN) clang-format --dry-run --Werror $(CPP_FILES)

lint:
	$(RUN) pio check -e native

check: lock-check format-check lint test build

upload: validate-idf validate-submodules
	$(IDF_RUN) $(IDF_ARGS) -b 1500000 $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) flash

migrate-storage-layout:
	@test -n "$(UPLOAD_PORT)" || (echo "UPLOAD_PORT is required for storage-layout migration." >&2; exit 2)
	$(MAKE) upload UPLOAD_PORT="$(UPLOAD_PORT)"
	esptool.py --chip esp32s3 --port "$(UPLOAD_PORT)" erase_region 0x7e0000 0x10000

monitor: validate-idf
	$(IDF_RUN) $(IDF_ARGS) $(if $(UPLOAD_PORT),-p $(UPLOAD_PORT),) monitor

clean: validate-idf
	$(IDF_RUN) $(IDF_ARGS) fullclean
