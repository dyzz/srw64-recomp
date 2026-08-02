VENV ?= .venv
PYTHON := $(VENV)/bin/python
ROM ?= rom.z64
W1_FONT ?= /Library/Fonts/RODE Noto Sans CJK SC R.otf
LIBRETRO_CORE ?= build/libretro/cores/mupen64plus_next_libretro.dylib
GLYPH_MAP ?= ref-project/SRW N64/reference/srw64_glyph_map_seed.csv
GLYPH_POLICY ?= ref-project/SRW N64/reference/srw64_unknown_glyph_policy.csv

.PHONY: bootstrap test check w0 w1 inventory libretro-smoke

bootstrap:
	python3 -m venv $(VENV)
	$(PYTHON) -m pip install --upgrade pip
	$(PYTHON) -m pip install -e . -r requirements.lock

test:
	$(PYTHON) -m unittest discover -s tests -v

check: test
	$(PYTHON) -m compileall -q src tools tests
	$(PYTHON) -m pip check

w0:
	$(PYTHON) -m srw64_w0.cli accept \
		--rom $(ROM) \
		--baseline config/srw64-jp-rev0.json \
		--work-dir build/w0

w1:
	$(PYTHON) -m srw64_w0.w1_cli \
		--rom $(ROM) \
		--config config/w1-slice.json \
		--font "$(W1_FONT)" \
		--work-dir build/w1

inventory:
	$(PYTHON) -m srw64_w0.inventory_cli \
		--rom $(ROM) \
		--baseline config/srw64-jp-rev0.json \
		--glyph-map "$(GLYPH_MAP)" \
		--unknown-glyph-policy "$(GLYPH_POLICY)" \
		--overlay translations/w1-slice.json \
		--work-dir build/text-inventory

libretro-smoke:
	$(PYTHON) tools/libretro_runner.py \
		--core $(LIBRETRO_CORE) \
		--rom build/w1/srw64-w1.zh-test.z64 \
		--work-dir build/libretro/start-scan \
		--frames 960 \
		--script config/libretro-w1-start-scan.json \
		--save-state build/libretro/start-scan/frame-000960.state
