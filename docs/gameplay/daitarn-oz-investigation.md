# BUG06: real protagonists on the OZ route omit Banjo's registration

Investigation date: 2026-11-01 (Singapore).

Source revisions inspected: v0.5.2 `96507db6e1484c213c73dede8882541f7cd3e709`
and main `3c6742cb0cfa4467292b4d7512ffc52dc1ab8024`.
The initial investigation established the defect in both revisions. A local
always-on repair and old-save migration have now been implemented, as described
below and in [base fixes](base-fixes.md#5-bug06真实系-oz-万丈泰坦3漏加入2026-11-01).
The published 0.5.2 is unchanged; this repair has not been released.

## Finding

The OZ route uses a command that unhides an existing pilot/machine pair where
the real protagonists need a command that creates the pair. The original defect
is therefore still present in the scripts used by both inspected revisions.

The ending event of S55, OZ episode 30 「その拳に心宿して」, starts at ROM
`0x001c586c`. Its common section contains this sequence:

| Event byte offset | Command | Effect |
| --- | --- | --- |
| 696 (`0x2b8`) | `3D6B 165,216,0` | Clear the current-party exclusion flags for an existing Banjo/Daitarn pair |
| 704 | `3D5A 201,0,270,273` | Eiji's New Layzner registration/swap |
| 714 | `3D5A 7,0,8,500` | Rain/Rising Gundam registration |
| 724 | `3D4B 56` | Continue to the next scene |

The defective command is at ROM `0x001c5b24`. There is no Banjo `3D5A`
registration in this event, nor a Banjo deployment record in S55.

## Why the protagonist matters

Super protagonists acquire Banjo/Daitarn through player-side deployment records
in their opening stages (S0/S1). They can therefore use the existing-pair
unhide command. Neither real protagonist acquires the pair on the paths to S55.

The audit constructs each real protagonist's scene graph from the original
`3D4B` instructions and intersects reachability from the opening with reverse
reachability from S55. No scene in that intersection supplies the pair, even
when every deployment record is conservatively treated as if it executed.
Variable guards are ignored, so the graph is an over-approximation, not a full
story replay. Link-stage returns and user MODs are outside this graph check.

The normal Complete Peace route provides a direct comparison. S69
「地球圏を覆う暗雲」, event `0x001b8640`, explicitly distinguishes the cases:

| Event byte offset | Section | Command |
| --- | --- | --- |
| 98 | `3DD5` (real protagonists) | section marker |
| 100 | real | `3D5A 165,0,216,500` — register the missing pair |
| 110 | `3DD6` (super protagonists) | section marker |
| 112 | super | `3D6B 165,216,0` — unhide the existing pair |
| 120 | `3DD0` | return to common section |

The Independence Army route also has real player-side Banjo deployment records
and corresponding `3D45` commands in S43/S44. Neither is a predecessor of S55.

## Why the space branch restores him

S102 「遥かなる賭け」 has Banjo/Daitarn in its player-side group 12 (record
`0x0020b00c`). Its opening event `0x001dd030`, byte offset 792, executes
`3D45 12`, before the general sortie selection at offset 796. Original
player-side deployment creates the persistent pair as well as placing it on
the map. This explains recovery without a separate Banjo `3D5A` command.

The earth branch goes to S103; its reachable finale scenes are
S103/S104/S105/S106/S134. None supplies the missing pair. Clearing all party
exclusions later cannot create absent records. Importantly, an existing Banjo
is intentionally excluded on the earth sub-party: `3D73 1` selects the list at
`800D06A8`, which contains pilot 165. The space choice's exclusion list at
`800D06E4` does not contain him. S104's opening clears these exclusions with
`3D74`. A save repair must preserve this temporary split, then restore him at
reunion, rather than allowing him to sortie on the earth sub-party.

## Why `3D59` is not the persistent cause

S55 event `0x001c5290`, offset 652, executes `3D59 165`; offset 656 similarly
excludes Rain, and offset 660 selects eight units via `3D3D`. These are
restrictions for that sortie. The non-zero-count `3D3D` clears the exclusion
list, and `3D4D` also clears it. The lasting defect is the missing registration
at the end of the stage, not this temporary restriction.

## Executed evidence

Run:

```sh
PYTHONPATH=src python tools/recomp/gameplay/check_daitarn_oz.py --machine-code
PYTHONPATH=src python -m unittest discover -s tests -p test_original_scripts.py -q
```

The audit first validates the pinned ROM SHA-256 and layout. Its optional
Unicorn checks execute the unmodified original `3D6B` handler at `0x800a158c`,
including `0x800ac6c4` and its callees, with synthetic RAM. No game functions or
OS services are stubbed. Both calls return and advance the script PC by six
bytes:

| Initial state | Result after `3D6B 165,216,0` |
| --- | --- |
| Missing pilot and machine | Both complete player tables remain byte-for-byte unchanged; no records are created |
| Existing pair, excluded | Pilot flags `0x81 → 0x01`; machine flags `0xc0 → 0x80`; pair remains registered |

The second case uses a valid team level of 1 and pilot level 99 so that the
routine's independent catch-up-level branch is not invoked. This is a bounded
machine-code test, not an Android or full-route replay. All 17 existing original
script tests also pass with the ROM present; none is skipped.

## Version status and implemented repair

Neither v0.5.2 nor the inspected main has a fix for this missing registration.
The native registration wrapper preserves the original registration call and
adds upgrade/parts bookkeeping; it does not synthesize a Banjo join. The
deployment wrapper changes boss dummy scaling, not join conditions. The
hidden-element inheritance rules do not contain pilot 165 or machine 216.

The local repair wraps `800A158C`, recognizes the original S55 ending tail, and
registers the absent pair for real protagonists before running the original
unhide. Other protagonists, scenes, changed scripts, existing records and
custom campaigns/mini stages are left alone. The other registrations and the
next-scene transition are preserved.

An idle intermission also repairs missing old saves, gated by the real
protagonist marker, var10=1 and the explicit set of OZ scenes reachable after
S55. Link stages use their saved story-return scene. In intermissions before
S103/S134/S104, the original party-flag function is called to keep Banjo in the
space sub-party until the original reunion. Any existing pilot or Daitarn-form
record, including a hidden one, suppresses migration so no existing upgrades,
experience or parts are overwritten. There is no save-format change.

`check_daitarn_oz.py --machine-code --repair-checks` compiles the actual C++
policy and connects its callbacks to the original MIPS registration/party
routines. Both real protagonists acquire pilot 165 and forms 216/217/218.
Old-save cases before S56/S103/S104/S105 pass, repeated migration leaves all RAM
unchanged, and S104's original reunion function clears the restored exclusion.
Only synchronous ROM-read I/O is substituted with a byte-identical copy in
these registration checks; no gameplay routine is stubbed. Native tests and
`tests/test_daitarn_oz.py` also cover negative cases and the ROM-derived scene
allowlist. These bounded tests do not claim a full Android route replay.
