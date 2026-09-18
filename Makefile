VENV ?= .venv
PYTHON := $(VENV)/bin/python
ROM ?= rom.z64
NATIVE_CXX ?= clang++
NATIVE_TEST_FLAGS := -std=c++20 -fsanitize=address,undefined -g
RECOMP_BUILD := build/recomp
RECOMP_RUNTIME := $(RECOMP_BUILD)/upstream/N64ModernRuntime

.PHONY: bootstrap test check recomp-bootstrap recomp-layout recomp-scan recomp-lz recomp-cpu recomp-audio-queue-test recomp-intro-test

bootstrap:
	python3 -m venv $(VENV)
	$(PYTHON) -m pip install --upgrade pip
	$(PYTHON) -m pip install -e . -r requirements.lock

test:
	$(PYTHON) -m unittest discover -s tests -v

check: test
	$(PYTHON) -m compileall -q src tools tests
	$(PYTHON) -m pip check

recomp-bootstrap:
	python3 tools/recomp/toolchain/bootstrap.py

recomp-layout:
	python3 tools/recomp/toolchain/analyze_layout.py --rom $(ROM)

recomp-scan:
	python3 tools/recomp/toolchain/scan_functions.py --rom $(ROM)

recomp-lz:
	$(PYTHON) tools/recomp/probes/run_lz_probe.py --limit 0

recomp-cpu:
	python3 tools/recomp/toolchain/generate_cpu.py

recomp-audio-queue-test:
	mkdir -p build/recomp
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -I src/host tests/native_audio_queue.cpp -o build/recomp/native-audio-queue-test
	build/recomp/native-audio-queue-test

recomp-intro-test:
	mkdir -p build/recomp/intro
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -I src/host tests/native_intro.cpp -o build/recomp/intro/controls-test
	build/recomp/intro/controls-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -I src/host -I build/recomp/upstream/RT64/src/contrib -I build/recomp/upstream/N64Recomp/include -I build/recomp/cpu-bound/generated -I build/recomp/upstream/N64ModernRuntime/librecomp/include/librecomp -I build/recomp/upstream/N64ModernRuntime/thirdparty tests/native_intro_adapter.cpp src/host/native_intro.cpp -o build/recomp/intro/adapter-test
	build/recomp/intro/adapter-test

.PHONY: recomp-content-test recomp-name-entry-test
recomp-name-entry-test:
	mkdir -p build/recomp/name-input
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Isrc/native -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include -Ibuild/recomp/cpu-bound/generated tests/native_name_entry.cpp src/host/native_name_entry.cpp -o build/recomp/name-input/name-entry-test
	build/recomp/name-input/name-entry-test

recomp-content-test:
	cmake --build build/recomp/gfx-build --target srw64-content-test srw64-dialogue-test -j 6
	build/recomp/gfx-build/srw64-content-test
	build/recomp/gfx-build/srw64-dialogue-test

# Native components require the pinned toolchain/generated headers. Keep this
# separate from the ROM-independent Python `check` target.
.PHONY: recomp-native-check recomp-timer-test recomp-replay-test
recomp-native-check: recomp-audio-queue-test recomp-intro-test recomp-name-entry-test recomp-content-test recomp-timer-test recomp-guest-shutdown-test recomp-replay-test recomp-state-probe-test recomp-script-inject-test recomp-mini-stage-test recomp-rule-fixes-test recomp-base-fixes-test recomp-upgrade-rules-test recomp-upgrade-refund-test recomp-debug-protocol-test

.PHONY: recomp-state-probe-test
recomp-state-probe-test:
	mkdir -p build/recomp/state-probe
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include tests/native_state_probe.cpp -o build/recomp/state-probe/test
	build/recomp/state-probe/test

.PHONY: recomp-guest-shutdown-test
recomp-guest-shutdown-test:
	$(PYTHON) tools/recomp/toolchain/prepare_runtime_lifecycle.py
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -I$(RECOMP_BUILD)/runtime-lifecycle -I$(RECOMP_RUNTIME)/ultramodern/include -I$(RECOMP_RUNTIME)/thirdparty -I$(RECOMP_RUNTIME)/thirdparty/concurrentqueue tests/native_guest_shutdown.cpp $(RECOMP_RUNTIME)/ultramodern/src/threadqueue.cpp -o $(RECOMP_BUILD)/runtime-lifecycle/guest-shutdown-test
	$(RECOMP_BUILD)/runtime-lifecycle/guest-shutdown-test

recomp-timer-test:
	$(PYTHON) tools/recomp/toolchain/prepare_runtime_lifecycle.py
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -I$(RECOMP_BUILD)/runtime-lifecycle -I$(RECOMP_RUNTIME)/ultramodern/include -I$(RECOMP_RUNTIME)/thirdparty -I$(RECOMP_RUNTIME)/thirdparty/concurrentqueue tests/native_timer_shutdown.cpp -o $(RECOMP_BUILD)/runtime-lifecycle/timer-test
	$(RECOMP_BUILD)/runtime-lifecycle/timer-test

recomp-replay-test:
	mkdir -p $(RECOMP_BUILD)
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) tests/native_replay_vi.cpp -o $(RECOMP_BUILD)/replay-vi-test
	$(RECOMP_BUILD)/replay-vi-test

.PHONY: recomp-data
recomp-data:
	$(PYTHON) tools/content/extract_original.py --rom $(ROM)

.PHONY: recomp-script-inject-test
recomp-script-inject-test:
	mkdir -p build/recomp/script-inject
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib tests/native_script_inject.cpp -o build/recomp/script-inject/test
	rm -rf build/recomp/script-inject/test-run && build/recomp/script-inject/test build/recomp/script-inject/test-run

.PHONY: recomp-upgrade-rules-test
recomp-upgrade-rules-test:
	mkdir -p build/recomp/upgrade-rules-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include tests/native_upgrade_rules.cpp -o build/recomp/upgrade-rules-test/test
	rm -rf build/recomp/upgrade-rules-test/test-run && build/recomp/upgrade-rules-test/test build/recomp/upgrade-rules-test/test-run $(ROM)

.PHONY: recomp-upgrade-refund-test
recomp-upgrade-refund-test:
	mkdir -p build/recomp/upgrade-refund-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include tests/native_upgrade_refund.cpp -o build/recomp/upgrade-refund-test/test
	rm -rf build/recomp/upgrade-refund-test/test-run && build/recomp/upgrade-refund-test/test build/recomp/upgrade-refund-test/test-run

.PHONY: recomp-rule-fixes-test
recomp-rule-fixes-test:
	mkdir -p build/recomp/rule-fixes-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include tests/native_rule_fixes.cpp -o build/recomp/rule-fixes-test/test
	rm -rf build/recomp/rule-fixes-test/test-run && build/recomp/rule-fixes-test/test build/recomp/rule-fixes-test/test-run

.PHONY: recomp-base-fixes-test
recomp-base-fixes-test:
	mkdir -p build/recomp/base-fixes-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Isrc/host -Ibuild/recomp/upstream/N64Recomp/include tests/native_base_fixes.cpp -o build/recomp/base-fixes-test/test
	build/recomp/base-fixes-test/test

.PHONY: recomp-mini-stage-test
recomp-mini-stage-test:
	mkdir -p build/recomp/mini-stage-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Wno-deprecated-declarations -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib -Ibuild/recomp/upstream/N64Recomp/include tests/native_mini_stage.cpp -o build/recomp/mini-stage-test/test
	rm -rf build/recomp/mini-stage-test/test-run && build/recomp/mini-stage-test/test build/recomp/mini-stage-test/test-run

.PHONY: recomp-debug-protocol-test
recomp-debug-protocol-test:
	mkdir -p build/recomp/debug-protocol-test
	$(NATIVE_CXX) $(NATIVE_TEST_FLAGS) -Isrc/host -Ibuild/recomp/upstream/RT64/src/contrib tests/native_debug_protocol.cpp -o build/recomp/debug-protocol-test/test
	build/recomp/debug-protocol-test/test
