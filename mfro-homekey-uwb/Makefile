# mfro-homekey-uwb build wrapper.
#
#   make build            configure + build (first run builds connectedhomeip: slow)
#   make flash [PORT=..]  flash over the XIAO's USB-C (USB-Serial-JTAG)
#   make monitor          serial console (ctrl-] to quit)
#   make go               build + flash + monitor
#   make menuconfig | size | fullclean
#
SHELL := /bin/bash

# Toolchain locations (override on the command line or in the environment):
IDF_ACTIVATE      ?= $(HOME)/.espressif/tools/activate_idf_v5.5.4.sh
ESP_MATTER_PATH   ?= /mnt/rocket/code/esp/esp-matter
ULTRAWIDELOCK_PATH ?= $(abspath $(CURDIR)/../../ultrawidelock)
BUILD             ?= build
PORT              ?=

ENV := export ESP_MATTER_PATH="$(ESP_MATTER_PATH)" ULTRAWIDELOCK_PATH="$(ULTRAWIDELOCK_PATH)" && \
       { . "$(IDF_ACTIVATE)" >/dev/null 2>&1; true; } && \
       . "$(ESP_MATTER_PATH)/export.sh" >/dev/null 2>&1

# The generated sdkconfig lives in the build dir, so `make fullclean` resets it
# to sdkconfig.defaults (a Kconfig default only seeds a fresh sdkconfig).
IDFPY := $(ENV) && idf.py -B "$(BUILD)" -D IDF_TARGET=esp32c6 -D SDKCONFIG="$(BUILD)/sdkconfig" \
         $(if $(PORT),-p $(PORT))

# ESP-IDF only reads sdkconfig.defaults when it creates a NEW sdkconfig; edits
# to it are otherwise silently ignored. Regenerate whenever it is newer.
# (menuconfig changes are lost then: put them in sdkconfig.defaults.)
.PHONY: sdkconfig-sync
sdkconfig-sync:
	@if [ -f "$(BUILD)/sdkconfig" ] && [ sdkconfig.defaults -nt "$(BUILD)/sdkconfig" ]; then \
		echo "sdkconfig.defaults changed: regenerating $(BUILD)/sdkconfig"; \
		rm -f "$(BUILD)/sdkconfig"; \
	fi

.PHONY: build flash monitor go menuconfig size fullclean env
build: sdkconfig-sync
	@$(IDFPY) build
flash: sdkconfig-sync
	@$(IDFPY) flash
monitor:
	@$(IDFPY) monitor
go: sdkconfig-sync
	@$(IDFPY) build flash monitor
menuconfig:
	@$(IDFPY) menuconfig
size:
	@$(IDFPY) size
fullclean:
	rm -rf "$(BUILD)"
env:
	@$(ENV) && idf.py --version && echo "ESP_MATTER_PATH=$$ESP_MATTER_PATH" && echo "ULTRAWIDELOCK_PATH=$$ULTRAWIDELOCK_PATH"
