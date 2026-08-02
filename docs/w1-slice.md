# W1: Chinese vertical slice

W1 proves one small but complete localization path before mass translation. The
50-row overlay in `translations/w1-slice.json` is a technical sample, not final
translation copy.

## Covered surfaces

- six title option rows;
- 24 battle/system command rows;
- 16 new-game route selection rows;
- two `FFFD` stop-control dialogues, including the female-route butler scene;
- one long, multi-page `FFFE`/`FFFD` dialogue;
- one dialogue containing five preserved name-placeholder glyphs;
- one deliberately expanded menu row that must be moved to the text pool.

Targets use explicit structural tokens: `<BR>`, `<STOP>`, `<END>`, and
`<G:0124>`. Validation rejects missing, added, or reordered structural tokens.

## Build

```sh
.venv/bin/python -m pip install -e . -r requirements.lock
.venv/bin/srw64-w1 \
  --rom rom.z64 \
  --config config/w1-slice.json \
  --font "/Library/Fonts/RODE Noto Sans CJK SC R.otf" \
  --work-dir build/w1
```

The font is a local input and is not copied into the project. Its SHA-256 is
gated by the W1 configuration. Every Han character in the target slice is
redrawn from the Simplified Chinese font even if the Japanese map contains the
same Unicode character.

## Static acceptance

The build must verify:

1. exact source ROM, glyph-map, overlay, configuration, and font hashes;
2. exact preservation and ordering of all control and raw-glyph tokens;
3. deterministic allocation within the added `081F-0AA6` glyph range;
4. font LZ encode/decode round-trip and fixed resource-pool bounds;
5. explicit text-pool bounds and successful reparsing of every patched row;
6. all byte differences fall inside resource/text descriptor, payload, or pool
   whitelist ranges;
7. the checksum-covered first MiB and original ROM size remain unchanged.

Generated ROM-derived files are written under `build/w1/` and ignored by source
control. `catalog.csv` is the human review surface; `build-report.json` and
`diff-ranges.csv` are the reproducibility evidence.

## Libretro runtime automation

`tools/libretro_runner.py` is the preferred deterministic macOS runtime path.
It is a small Libretro frontend that loads the ROM and an arm64 core directly,
feeds frame-addressed RetroPad states, captures software-rendered PNG frames,
and loads or saves core serialization states. It does not synthesize keyboard
events and does not require a focused emulator window. Here, deterministic
means the ROM/core identity and frame-addressed control path are explicit; core
save-state bytes can contain internal runtime state and are not required to
produce the same hash on separate runs.

The installed local core is
`build/libretro/cores/mupen64plus_next_libretro.dylib`. The runner forces the
software Angrylion RDP and CXD4 RSP so screenshots are available without a GPU
window. It was installed on 2026-08-02 from the official
[Libretro Apple arm64 buildbot](https://buildbot.libretro.com/nightly/apple/osx/arm64/latest/mupen64plus_next_libretro.dylib.zip);
the dylib SHA-256 is
`8cd7541261b06b89c18189d7621b825e4e6f906b64f4449056a40d0647a6f58d`.
A boot and Start-button scan is reproducible with:

```sh
.venv/bin/python tools/libretro_runner.py \
  --core build/libretro/cores/mupen64plus_next_libretro.dylib \
  --rom build/w1/srw64-w1.zh-test.z64 \
  --work-dir build/libretro/start-scan \
  --frames 960 \
  --script config/libretro-w1-start-scan.json \
  --save-state build/libretro/start-scan/frame-000960.state
```

Input scripts under `config/libretro-w1-*.json` cover boot/Start, common
prologue advancement, route cycling, the female super-robot choice, default
name confirmation, final route confirmation, and the `t00_17410` `<STOP>`
boundary. Each run emits `run-report.json`, a contact sheet, individual PNGs,
and an optional checkpoint. The report records the input-script hash and, when
used, the loaded checkpoint hash. Mupen64Plus-Next initializes plugin state
lazily, so the runner performs one no-input warm-up frame before restoring a
state.

The current W1 ROM SHA-256 is
`c5f6c1895ae869e6744e7a9b1fb79068cb5b7f6acafa155252ea29fce82d9ac1`.
Runtime proof for the target row is in:

- `build/libretro/target-dialogue/screenshots/frame-000600.png`: first two
  Chinese lines before `<STOP>`;
- `build/libretro/target-stop/screenshots/frame-000060.png`: final Chinese line
  after one N64 A press;
- the corresponding `run-report.json` files: exact core/ROM hashes, frame
  counts, captures, options, and save-state hashes.

## ares runtime boundary

ares v148 can load the ROM and configure the debug server entirely through its
command line. RSP provides CPU/memory breakpoints and assertions. ares does not
expose controller-event or headless framebuffer commands through RSP, so menu
navigation and screenshots use fixed keyboard mappings plus a local UI driver.

The isolated command-line runner uses RSP port 9124 and never edits the normal
ares settings file:

```sh
.venv/bin/python tools/ares_w1_runner.py start
.venv/bin/python tools/ares_rsp_probe.py --host ::1 --port 9124
.venv/bin/python tools/ares_w1_runner.py status
.venv/bin/python tools/ares_w1_runner.py stop
```

The fixed mappings are arrow keys for the D-pad, `J` for A, `K` for B, Return
for Start, F10/F11 for load/save state, and F12 for an ares framebuffer
screenshot.
