# make sim     desktop simulator (ESPHome host build, SDL window); bridge talks to it on 127.0.0.1.
#              Rebuilds and relaunches 2 s after the last change to the UI / component sources or configs.
# make sim-panel  controls for the sim: pick a bot, pin it working or idle (build/sim-pin.jsonl)
# make test    test:cpp, then test:node
#   test:cpp   host C++ tests (tests/test_*.cpp)
#   test:node  the bridge's npm test (node --test)
# make flash   build and OTA the dial (DIAL=ip overrides; else .env.dialip, last good IP, found via mDNS if empty).
#              Posts the result to the notify channel (.claude/channel/notify.ts) if a Claude session runs it.
# make voice   start the voice assistant (tmux session "voice": claude in ~/workspace/voice-assistant) if absent.
# Pets load from bridge/assets (DIAL_ASSETS overrides). Logs land in build/*.log as well as the terminal.

DIAL ?=
DIAL_ASSETS ?= $(CURDIR)/bridge/assets
export DIAL_ASSETS
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c  # tee must not hide a failed build

.PHONY: sim sim-panel voice flash dialip test test\:cpp test\:node logs
# .env.dialip empty or missing: look the dial up on the network and record it. Otherwise nothing to do.
dialip:
	@[ -s .env.dialip ] || { ip=$$(scripts/find-dial.sh) && echo "$$ip" > .env.dialip && echo "dial at $$ip (.env.dialip)"; }

test:  # make 3.81 drops escaped-colon prerequisites, so run the two by name
	@$(MAKE) --no-print-directory test:cpp test:node

test\:cpp:
	@mkdir -p build
	@for t in tests/test_*.cpp; do \
	  b=build/$$(basename $$t .cpp); \
	  c++ -std=c++17 -Wall -DSCENE_NO_LVGL -DCUBE_NO_LVGL $$t -o $$b && $$b || exit 1; \
	done

test\:node:
	cd bridge && npm test

sim: dialip
	@mkdir -p build
	@pkill -f '[w]atchexec.*claude-buddy-sim' || true  # an older make sim would keep relaunching
	@pkill -f '[c]laude-buddy-sim/program' || true  # an old sim still holds port 6053
	watchexec --restart --debounce 2s --exts h,cpp,py,yaml,mjs \
	  -w buddy -w components -w scripts -w buddy-ui.yaml -w claude-buddy-sim.yaml \
	  -- esphome run claude-buddy-sim.yaml 2>&1 | tee build/sim.log

sim-panel:  # controls for the sim (scripts/sim-panel.mjs): pick a bot, pin it working or idle
	@(sleep 1; open http://localhost:19880) &
	node scripts/sim-panel.mjs

voice:  # the router's "assistant" target types into this session; absent = the card says "Assistant offline"
	@tmux has-session -t =voice 2>/dev/null && echo "tmux session voice already running" || \
	  { tmux new-session -d -s voice -c $(HOME)/workspace/voice-assistant claude && echo "started tmux session voice (tmux attach -t voice)"; }

flash: dialip
	@mkdir -p build
	ip=$(or $(DIAL),$$(cat .env.dialip)); \
	esphome run claude-buddy.yaml --device $$ip --no-logs 2>&1 | tee build/flash.log; \
	if grep -q "OTA successful" build/flash.log; then echo "$$ip" > .env.dialip; msg="OTA successful"; rc=0; \
	else msg="FAILED: $$(grep -E 'ERROR|error:' build/flash.log | tail -1)"; rc=1; fi; \
	./notify.sh "make flash $$ip: $$msg"; \
	exit $$rc

logs: dialip
	ip=$(or $(DIAL),$$(cat .env.dialip)); \
	esphome logs claude-buddy.yaml --device $$ip --no-states
