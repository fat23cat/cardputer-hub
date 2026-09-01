UV ?= uv
RUN := $(UV) run --frozen
CPP_FILES := $(shell find src test -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) | sort)

.PHONY: setup lock-check build test format format-check lint check upload migrate-storage-layout monitor clean

setup:
	$(UV) sync --frozen

lock-check:
	$(UV) lock --check

build:
	$(RUN) pio run -e cardputer-adv

test:
	$(RUN) python -m unittest discover -s test_python
	$(RUN) pio test -e native

format:
	$(RUN) clang-format -i $(CPP_FILES)

format-check:
	$(RUN) clang-format --dry-run --Werror $(CPP_FILES)

lint:
	$(RUN) pio check -e native
	$(RUN) pio check -e cardputer-adv

check: lock-check format-check lint test build

upload:
	$(RUN) pio run -e cardputer-adv --target upload

migrate-storage-layout: upload
	$(RUN) pio pkg exec --package tool-esptoolpy -- esptool.py --chip esp32s3 erase_region 0x7e0000 0x10000

monitor:
	$(RUN) pio device monitor --baud 115200

clean:
	$(RUN) pio run --target clean
