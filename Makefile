# make sim     desktop simulator (ESPHome host build, SDL window); bridge talks to it on 127.0.0.1.
#              Rebuilds and relaunches 2 s after the last change to the UI / component sources or configs.
# make flash   build and OTA the dial (DIAL=ip overrides; else .env.dialip, last good IP, found via mDNS if empty)
# Pets load from bridge/assets (DIAL_ASSETS overrides). Logs land in build/*.log as well as the terminal.

DIAL ?=
DIAL_ASSETS ?= $(CURDIR)/bridge/assets
export DIAL_ASSETS
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c  # tee must not hide a failed build

.PHONY: sim flash dialip
# .env.dialip empty or missing: look the dial up on the network and record it. Otherwise nothing to do.
dialip:
	@[ -s .env.dialip ] || { ip=$$(scripts/find-dial.sh) && echo "$$ip" > .env.dialip && echo "dial at $$ip (.env.dialip)"; }

sim: dialip
	@mkdir -p build
	@pkill -f '[w]atchexec.*claude-buddy-sim' || true  # an older make sim would keep relaunching
	@pkill -f '[c]laude-buddy-sim/program' || true  # an old sim still holds port 6053
	watchexec --restart --debounce 2s --exts h,cpp,py,yaml,mjs \
	  -w buddy -w components -w scripts -w buddy-ui.yaml -w claude-buddy-sim.yaml \
	  -- esphome run claude-buddy-sim.yaml 2>&1 | tee build/sim.log

flash: dialip
	@mkdir -p build
	ip=$(or $(DIAL),$$(cat .env.dialip)); \
	esphome run claude-buddy.yaml --device $$ip --no-logs 2>&1 | tee build/flash.log && echo "$$ip" > .env.dialip
