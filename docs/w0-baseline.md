# W0: lossless SRW64 baseline

W0 establishes an exact-ROM gate and a lossless representation of all text-table
entries before translation begins. It does not modify the Korean reference project
and it does not assign display semantics to control words that have not yet been
proven at runtime.

## Trust boundaries

- Supported input: the exact Japanese Rev 0 ROM declared in
  `config/srw64-jp-rev0.json`.
- Reference project: pinned read-only format oracle at commit
  `91e0c15b76b44c302f29ddebc1c45e61f1828cd0`.
- Generated data: `build/w0/`; excluded from source control because it includes
  extracted game data and a reconstructed ROM.
- W0 runtime dependencies: Python standard library only.
- Font/image dependency for later stages: Pillow pinned in `requirements.lock`.

## Lossless IR

Each JSONL record retains the original descriptor location, relative/data offsets,
byte size, eight-byte entry header, and every unsigned 16-bit stream unit. Control
words are never flattened into display text:

```json
{"key":"t00_00000","header_hex":"0000000000000000","units_u16":"0124 FFFD FFFE FFFF"}
```

Known structural words remain distinct:

- `FFFF`: stream terminator
- `FFFE`: control word; display semantics must be runtime-tested
- `FFFD`: different control word; display semantics must be runtime-tested

The later translator-facing format must reference this IR and preserve required
control/glyph tokens rather than replacing the canonical stream.

## Reproducible commands

Create the isolated environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e . -r requirements.lock
```

Run unit tests:

```sh
.venv/bin/python -m unittest discover -s tests -v
```

Run the full local acceptance flow:

```sh
.venv/bin/srw64-w0 accept \
  --rom rom.z64 \
  --baseline config/srw64-jp-rev0.json \
  --work-dir build/w0
```

The acceptance command must produce all of the following:

1. exact ROM identity gate passes;
2. every text table and entry validates structurally;
3. extracted JSONL hash is recorded in `manifest.json`;
4. every JSONL record matches its source ROM bytes;
5. `rom.noop.z64` is byte-identical to the input ROM;
6. `acceptance.json` records the evidence without timestamps or machine-specific
   absolute paths.

## Verified local baseline

The W0 acceptance flow passed against the declared local ROM on 2026-08-02:

- ROM size: 33,554,432 bytes
- ROM and no-op output SHA-256:
  `ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`
- text tables: 20
- text entries: 51,174
- stream units: 1,341,140
- `FFFF`: 51,174
- `FFFE`: 29,562
- `FFFD`: 13,728
- canonical IR size: 19,476,727 bytes
- canonical IR SHA-256:
  `b46200be3a1419f89e00f92ea433061761374e6e32a10e53a5ff83f995e39c39`

The generated evidence is in `build/w0/acceptance.json`. It is deliberately
ignored by source control because adjacent artifacts contain extracted game data
and a complete ROM reconstruction.
