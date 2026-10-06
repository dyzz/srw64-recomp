---
title: Stage script reference
summary: Every command, condition, context marker, event type, variable and deployment record field of the stage event scripts, for writing mini stages.
section: tools
order: 3
updated: 2026-10-07
---

Each event in a stage is a run of 16-bit words: a command opcode `3D31`–`3D79` followed by its operands, conditions `3E00`–`3E1D` forming if-blocks, context markers `3DD0`–`3DDB` that pick the passages for the current hero’s route or a choice, and `FFFF` to end the event. In a [mini stage](../mini-stage/) a command is written as `{"op": "3D45", "args": [1]}`; conditions and markers are written the same way.

This page is generated from the `stage_scripts` table in [`config/data/original-jp-v1.json`](https://github.com/dyzz/srw64-recomp/blob/main/config/data/original-jp-v1.json). “Confirmed in code” means the meaning was read from the game’s handler; “structure confirmed” means what it reads and writes is known but not fully what it is for.

The implementation notes (which game code each entry rests on) and the in-game findings are recorded in Chinese; every entry links to them on the [Chinese page](../../../zh/docs/script-commands/).

## Event types

An event’s type decides when it is checked and how its four trigger parameters (`header`) are read.

| Type | Name | Checked | Trigger parameters |
|---|---|---|---|
| 0 | Turn start | Phase 0 | 1. Turns: Current turn ≥ this value<br>2. Phase: Must equal the current phase 8010F5E8 (1 player / 2 enemy)<br>3. Reserved<br>4. Reserved |
| 1 | Delayed count (started by 3D52) | Phase 0 | 1. Initial state: Rewritten to 0xFD by 3D52 at run time<br>2. Phase: Rewritten by 3D52 at run time; 4 = any<br>3. Reserved<br>4. Reserved |
| 2 | Unit destroyed / withdrawn | Always | 1. Gate variable: For 100–115 the event does not fire while the variable is 3 (8009DE7C sets 100–114 to 3 at the start of each stage; a script enables it by writing 0); other numbers are not checked; 0 means no gate<br>2. Character: The unit this character pilots reaches 0% HP<br>3. Reserved<br>4. Reserved |
| 3 | HP below a percentage | Always | 1. Character: Special case for character 138: continues only when its unit's HP < 11<br>2. Percentage: HP% ≤ this value and on the map<br>3. Reserved<br>4. Reserved |
| 4 | Battle event (after battle, phase 7) | Phase 7 | 1. Character A: 0 = any<br>2. Character B: 0 = any<br>3. Reserved<br>4. Reserved |
| 5 | Battle event (before battle, phase 4) | Phase 4 | 1. Character A: 0 = any<br>2. Character B: 0 = any<br>3. Reserved<br>4. Reserved |
| 6 | All enemies destroyed | Always | 1. Latest turn: Fires only while the current turn (from 0) ≤ this value; 8009E834 returns once it is below the current turn<br>2. Phase: 4 = any<br>3. Reserved<br>4. Reserved |
| 7 | Units remaining on a side | Always | 1. Side choice: 2 → third-party count 0x9B2, otherwise the enemy count 0x9B1<br>2. Maximum count: Remaining ≤ this value<br>3. Phase: 4 = any<br>4. Gate variable: For 100–115 the event does not fire while the variable is 3 (8009DE7C sets 100–114 to 3 at the start of each stage; a script enables it by writing 0); other numbers are not checked |
| 8 | Area reached (enabled by 3D57) | Phase 0, Phase 1, Phase 2, Phase 6 | 1. Turn or mode: A number = current turn ≥ this value; 0xFF = fire on arrival; 0xFE = all allies have left<br>2. Target: N×1000 + character (or 500 + side; 21 = the first character in the list on the map)<br>3. x range: x0×10 + width<br>4. y range: y0×10 + height |
| 9 | Persuasion (0x992, phase 6) | Phase 0, Phase 1, Phase 2, Phase 6 | 1. Persuader: 800A4634 finds, in registration order, the first slot not yet run whose gate holds and whose two characters are orthogonally adjacent, and writes it to engine+0x992<br>2. Target: The target may be on any side<br>3. Gate variable: 0xFFF8 (−8) means no gate; otherwise Persuade appears only when the variable equals the gate value. Multi-step persuasion across stages is chained through it<br>4. Gate value |
| 10 | Registered but never polled | None | 1. Reserved<br>2. Reserved<br>3. Reserved<br>4. Reserved |
| 11 | Registered but never polled | None | 1. Reserved<br>2. Reserved<br>3. Reserved<br>4. Reserved |
| 12 | Opening event (phase C1) | engine+4 = 0xC1 | 1. Reserved<br>2. Reserved<br>3. Reserved<br>4. Reserved |
| 13 | Phase C2 event (initial deployment) | engine+4 = 0xC2 | 1. Reserved<br>2. Reserved<br>3. Reserved<br>4. Reserved |
| 14 | Ending event (phase C3, after 3D4A) | engine+4 = 0xC3 | 1. Reserved<br>2. Reserved<br>3. Reserved<br>4. Reserved |

## Commands

<a id="op-3d31"></a>

### 3D31 · World map: place at location (same handler as 3D32)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0E64`

1. Location — `world_location`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d31)

<a id="op-3d32"></a>

### 3D32 · World map: place at location

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0E64`

1. Location — `world_location`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d32)

<a id="op-3d33"></a>

### 3D33 · World map: travel to location

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0EFC`

1. Location — `world_location`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d33)

<a id="op-3d34"></a>

### 3D34 · Switch map and scroll to position (map, x, y, kind)

Operands (4 word(s)) · Confirmed in code · Handler `0x8009F6FC`

1. Map — `map`: Map number (base:map_assets; written to 8010F5EE)
2. x — `x`: Square x
3. y — `y`: Square y
4. Kind — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d34)

<a id="op-3d35"></a>

### 3D35 · Scroll camera to position (position, wait flag)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A03E8`

1. Position — `position`: High byte x / low byte y; 0x40–0x44 relative to the position 3D54 remembered
2. Wait flag — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d35)

<a id="op-3d36"></a>

### 3D36 · Shake screen (strength)

Operands (1 word(s)) · Confirmed in code · Handler `0x8009F880`

1. Strength — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d36)

<a id="op-3d37"></a>

### 3D37 · Map effect overlay (effect, position)

Operands (2 word(s)) · Confirmed in code · Handler `0x8009FF2C`

1. Effect — `raw`: Not yet understood
2. Position — `position`: High byte x / low byte y; 0x40–0x44 relative to the position 3D54 remembered

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d37)

<a id="op-3d38"></a>

### 3D38 · Wait (frames)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A00F0`

1. Frames — `count`: Count

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d38)

<a id="op-3d39"></a>

### 3D39 · Play sound effect

Operands (1 word(s)) · Confirmed in code · Handler `0x8009FA4C`

1. Sound effect — `se`: Sound effect number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d39)

<a id="op-3d3a"></a>

### 3D3A · Play BGM (0 = stop)

Operands (1 word(s)) · Confirmed in code · Handler `0x8009FA70`

1. Track — `bgm`: Track number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3a)

<a id="op-3d3b"></a>

### 3D3B · Fade screen (mode)

Operands (1 word(s)) · Confirmed in code · Handler `0x8009F948`

1. Mode — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3b)

<a id="op-3d3c"></a>

### 3D3C · Move unit to position (pilot, position)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A0360`

1. Pilot — `actor`: Character number (base:actors)
2. Position — `position`: High byte x / low byte y; 0x40–0x44 relative to the position 3D54 remembered

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3c)

<a id="op-3d3d"></a>

### 3D3D · Sortie (–, –, –, count: 0 = choose mothership, 200 = all automatic, otherwise the selection limit; base group)

Operands (5 word(s)) · Confirmed in code · Handler `0x800A09D0`

1. Reserved (original scripts copy the base x) — `raw`: Not yet understood
2. Reserved (original scripts copy the base y) — `raw`: Not yet understood
3. Parameter (always 0 in the original scripts) — `raw`: Not yet understood
4. Count (0 mothership / 200 automatic / otherwise limit) — `raw`: Not yet understood
5. Base group — `group`: Deployment record group

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3d)

<a id="op-3d3e"></a>

### 3D3E · Dialogue · display mode 0

Operands (1 word(s)) · Dialogue display mode 0 · Confirmed in code · Handler `0x8009F654`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3e)

<a id="op-3d3f"></a>

### 3D3F · Dialogue · display mode 1

Operands (1 word(s)) · Dialogue display mode 1 · Confirmed in code · Handler `0x8009F670`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d3f)

<a id="op-3d40"></a>

### 3D40 · Dialogue · display mode 2

Operands (1 word(s)) · Dialogue display mode 2 · Confirmed in code · Handler `0x8009F68C`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d40)

<a id="op-3d41"></a>

### 3D41 · Dialogue · display mode 3

Operands (1 word(s)) · Dialogue display mode 3 · Confirmed in code · Handler `0x8009F6A8`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d41)

<a id="op-3d42"></a>

### 3D42 · Dialogue · display mode 4

Operands (1 word(s)) · Dialogue display mode 4 · Confirmed in code · Handler `0x8009F6C4`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d42)

<a id="op-3d43"></a>

### 3D43 · Dialogue · display mode 5

Operands (1 word(s)) · Dialogue display mode 5 · Confirmed in code · Handler `0x8009F6E0`

1. Text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d43)

<a id="op-3d44"></a>

### 3D44 · Show a choice (window slot, option count, option text)

Operands (3 word(s)) · Confirmed in code · Handler `0x8009FA94`

1. Window slot — `raw`: Not yet understood
2. Option count — `count`: Count
3. Option text — `text`: Text number in table 0

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d44)

<a id="op-3d45"></a>

### 3D45 · Deploy a record group (arrival)

Operands (1 word(s)) · Confirmed in code · Handler `0x8009FC04`

1. Group — `group`: Deployment record group

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d45)

<a id="op-3d46"></a>

### 3D46 · Withdraw unit (character or group, immediately)

Operands (2 word(s)) · Confirmed in code · Handler `0x8009FDC4`

1. Character or group — `actor_or_group`: Below 500 a character number, from 500 a group (value − 500)
2. Immediately — `bool`: Switch

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d46)

<a id="op-3d47"></a>

### 3D47 · Close the dialogue window and wait 32 frames

No operands · Confirmed in code · Handler `0x800A013C`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d47)

<a id="op-3d48"></a>

### 3D48 · Close the dialogue window

No operands · Confirmed in code · Handler `0x800A0194`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d48)

<a id="op-3d49"></a>

### 3D49 · Scripted battle scene (battle entry A, battle entry B)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A10F0`

1. Battle entry A — `battle_entry`
2. Battle entry B — `battle_entry`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d49)

<a id="op-3d4a"></a>

### 3D4A · Stage victory

No operands · Confirmed in code · Handler `0x800A01CC`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4a)

<a id="op-3d4b"></a>

### 3D4B · Set the next scene (500 = restore 8010F5F2)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0260`

1. Scene — `scene`: Scene

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4b)

<a id="op-3d4c"></a>

### 3D4C · Game over

No operands · Confirmed in code · Handler `0x800A02E0`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4c)

<a id="op-3d4d"></a>

### 3D4D · Switch from the world map to the battlefield

No operands · Confirmed in code · Handler `0x800A031C`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4d)

<a id="op-3d4e"></a>

### 3D4E · Close windows and yield one frame

No operands · Confirmed in code · Handler `0x800A0468`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4e)

<a id="op-3d4f"></a>

### 3D4F · Destroy unit (character or group)

Operands (1 word(s)) · Confirmed in code · Handler `0x8009FE9C`

1. Character or group — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d4f)

<a id="op-3d50"></a>

### 3D50 · Transform unit (character, from unit, to unit)

Operands (3 word(s)) · Confirmed in code · Handler `0x800A04AC`

1. Character — `actor`: Character number (base:actors)
2. From unit — `unit`: Unit number (base:units)
3. To unit — `unit`: Unit number (base:units)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d50)

<a id="op-3d51"></a>

### 3D51 · Fire a MAP weapon (character, unit, MAP weapon, direction, –)

Operands (5 word(s)) · Confirmed in code · Handler `0x800A051C`

1. Character — `actor`: Character number (base:actors)
2. Unit — `unit`: Unit number (base:units)
3. MAP weapon — `weapon`
4. Direction — `raw`: Not yet understood
5. – — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d51)

<a id="op-3d52"></a>

### 3D52 · Start a delayed-count event (slot, turns, phase)

Operands (3 word(s)) · Confirmed in code · Handler `0x800A05D0`

1. Type 1 slot — `event_slot_type1`: Registration index of a type 1 event in this scene
2. Turns — `count`: Count
3. Phase — `side`: Phase 1 player / 2 enemy / 3 third party (4 = any)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d52)

<a id="op-3d53"></a>

### 3D53 · Stop a delayed-count event (slot)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0638`

1. Type 1 slot — `event_slot_type1`: Registration index of a type 1 event in this scene

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d53)

<a id="op-3d54"></a>

### 3D54 · Remember the character's position

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0678`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d54)

<a id="op-3d55"></a>

### 3D55 · Use a spirit / cut the camera to a character (character, spirit or 30)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A0714`

1. Character — `actor`: Character number (base:actors)
2. Spirit — `spirit`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d55)

<a id="op-3d56"></a>

### 3D56 · Highlight a green target area (encoded corner A, encoded corner B, –)

Operands (3 word(s)) · Confirmed in code · Handler `0x800A0780`

1. Corner A — `raw`: Not yet understood
2. Corner B — `raw`: Not yet understood
3. Reserved — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d56)

<a id="op-3d57"></a>

### 3D57 · Enable an area-arrival event slot (slot, value)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A0BD0`

1. Type 8 slot — `event_slot_type8`: Registration index of a type 8 event in this scene
2. Value — `bool`: Switch

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d57)

<a id="op-3d58"></a>

### 3D58 · Change a unit's side (character, new side)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A0C14`

1. Character — `actor`: Character number (base:actors)
2. New side — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d58)

<a id="op-3d59"></a>

### 3D59 · Bar from sorties (character; ≥500 means 500 + unit number)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0C80`

1. Character or 500 + unit — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d59)

<a id="op-3d5a"></a>

### 3D5A · Team roster change (character, parameter, unit, mode / old unit)

Operands (4 word(s)) · Confirmed in code · Handler `0x800A0B3C`

1. Character — `actor`: Character number (base:actors)
2. Parameter — `raw`: Not yet understood
3. Unit — `unit`: Unit number (base:units)
4. Mode / old unit — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5a)

<a id="op-3d5b"></a>

### 3D5B · Add funds: parameter × 1000

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0D24`

1. Thousands — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5b)

<a id="op-3d5c"></a>

### 3D5C · Combine / separate (character, 1 = combine, 0 = separate)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A0D68`

1. Character — `actor`: Character number (base:actors)
2. Mode — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5c)

<a id="op-3d5d"></a>

### 3D5D · Set a combining robot's form (1 = combined, 0 = separated; family 0 = Dancouga, 1 = Combattler V)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A1010`

1. Form — `raw`: Not yet understood
2. Family — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5d)

<a id="op-3d5e"></a>

### 3D5E · Open the team name entry screen

No operands · Confirmed in code · Handler `0x800A1050`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5e)

<a id="op-3d5f"></a>

### 3D5F · All pilots' morale −30 (minimum 50)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1078`

1. Parameter — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d5f)

<a id="op-3d60"></a>

### 3D60 · Line up the allied units (start x, start y, per row)

Operands (3 word(s)) · Confirmed in code · Handler `0x800A10AC`

1. x — `x`: Square x
2. y — `y`: Square y
3. Per row — `count`: Count

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d60)

<a id="op-3d61"></a>

### 3D61 · Suspend the "all allies destroyed means defeat" check (switch)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1328`

1. Suspend — `bool`: Switch

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d61)

<a id="op-3d62"></a>

### 3D62 · Replace a character's identity (character → character)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A1224`

1. Old character — `actor`: Character number (base:actors)
2. New character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d62)

<a id="op-3d63"></a>

### 3D63 · Land / take off (character)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1264`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d63)

<a id="op-3d64"></a>

### 3D64 · Move out a co-pilot (character; 500,500 = Aisha moves to Manami's mothership)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A12CC`

1. Character — `actor`: Character number (base:actors)
2. Mode — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d64)

<a id="op-3d65"></a>

### 3D65 · Set victory / defeat conditions (victory, defeat)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A1360`

1. Victory condition — `victory_condition`
2. Defeat condition — `defeat_condition`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d65)

<a id="op-3d66"></a>

### 3D66 · Don't move the camera to the speaker during dialogue (switch)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A13A4`

1. Don't follow the speaker — `bool`: Switch

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d66)

<a id="op-3d67"></a>

### 3D67 · Power-up / combination scene (scene number)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A13F0`

1. Scene number — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d67)

<a id="op-3d68"></a>

### 3D68 · Bring the camera back to the selected unit

No operands · Confirmed in code · Handler `0x800A1458`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d68)

<a id="op-3d69"></a>

### 3D69 · Set a unit's action state (character, 1 = can act, 0 = cannot act)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A14A4`

1. Character — `actor`: Character number (base:actors)
2. Action state — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d69)

<a id="op-3d6a"></a>

### 3D6A · Godmars combination (mode)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A14E4`

1. Mode — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6a)

<a id="op-3d6b"></a>

### 3D6B · Selection mark (character, unit, 1 = set, 0 = clear)

Operands (3 word(s)) · Confirmed in code · Handler `0x800A158C`

1. Character — `actor`: Character number (base:actors)
2. Unit — `unit`: Unit number (base:units)
3. Switch — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6b)

<a id="op-3d6c"></a>

### 3D6C · Set a unit's upgrade level (unit, levels)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A15D0`

1. Unit — `unit`: Unit number (base:units)
2. Levels — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6c)

<a id="op-3d6d"></a>

### 3D6D · Set the current phase to 1 (player phase)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1610`

1. Parameter — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6d)

<a id="op-3d6e"></a>

### 3D6E · Withdraw unit (character)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1644`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6e)

<a id="op-3d6f"></a>

### 3D6F · Unlock a weapon (weapon number)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A1680`

1. Weapon — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d6f)

<a id="op-3d70"></a>

### 3D70 · Add a co-pilot (pilot, co-pilot)

Operands (2 word(s)) · Confirmed in code · Handler `0x800A16BC`

1. Pilot — `actor`: Character number (base:actors)
2. Co-pilot — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d70)

<a id="op-3d71"></a>

### 3D71 · Go to the ending (does not return)

No operands · Confirmed in code · Handler `0x800A02B8`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d71)

<a id="op-3d72"></a>

### 3D72 · Set the world map vehicle (vehicle)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A0FCC`

1. Vehicle — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d72)

<a id="op-3d73"></a>

### 3D73 · Team split: select by preset list (1 = list A, 0 = list B)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A16FC`

1. List — `raw`: Not yet understood

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d73)

<a id="op-3d74"></a>

### 3D74 · Clear all team-split selection marks

No operands · Confirmed in code · Handler `0x800A1924`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d74)

<a id="op-3d75"></a>

### 3D75 · Launch units from a mothership (character; a captain launches the whole ship, 999 = 46 Bright)

Operands (1 word(s)) · Confirmed in code · Handler `0x800A197C`

1. Character (a captain launches the whole ship; 999 = 46) — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d75)

<a id="op-3d76"></a>

### 3D76 · No handler

No operands · Confirmed in code

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d76)

<a id="op-3d77"></a>

### 3D77 · No handler

No operands · Confirmed in code

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d77)

<a id="op-3d78"></a>

### 3D78 · No-op (unreachable)

No operands · Confirmed in code · Handler `0x800A01BC`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d78)

<a id="op-3d79"></a>

### 3D79 · No-op (unreachable)

No operands · Confirmed in code · Handler `0x800A01C4`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3d79)

## Conditions

Conditions test and set the ACC register and the variables. An “opener” starts a block; when it is false, the script skips to the matching “block end” (`3E1D`). Blocks nest.

<a id="op-3e00"></a>

### 3E00 · ACC = 0

No operands · Kind: statement · Confirmed in code · Handler `0x800A2388`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e00)

<a id="op-3e01"></a>

### 3E01 · If the character's HP percentage < threshold (and on the map), ACC = value

Operands (3 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A1FBC`

1. Character — `actor`: Character number (base:actors)
2. Percentage — `value`: Number
3. ACC value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e01)

<a id="op-3e02"></a>

### 3E02 · If variable ≠ value

Operands (2 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A207C`

1. Variable — `flag`: 2-bit variable 0–199
2. Value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e02)

<a id="op-3e03"></a>

### 3E03 · If variable = value, ACC = value 2

Operands (3 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A20C4`

1. Variable — `flag`: 2-bit variable 0–199
2. Value — `value`: Number
3. ACC value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e03)

<a id="op-3e04"></a>

### 3E04 · If the current turn < turns, ACC = value

Operands (2 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A2344`

1. Turns — `value`: Number
2. ACC value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e04)

<a id="op-3e05"></a>

### 3E05 · Always true (not implemented)

No operands · Kind: opener · Confirmed in code

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e05)

<a id="op-3e06"></a>

### 3E06 · ACC = !(level[character A] < level[character B])

Operands (2 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A22CC`

1. Character A — `actor`: Character number (base:actors)
2. Character B — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e06)

<a id="op-3e07"></a>

### 3E07 · Always true (not implemented)

No operands · Kind: opener · Confirmed in code

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e07)

<a id="op-3e08"></a>

### 3E08 · If ACC = value

Operands (1 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A23C4`

1. Value — `value`: Number

<a id="op-3e09"></a>

### 3E09 · If ACC ≥ value

Operands (1 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A23EC`

1. Value — `value`: Number

<a id="op-3e0a"></a>

### 3E0A · If ACC ≠ value

Operands (1 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A2438`

1. Value — `value`: Number

<a id="op-3e0b"></a>

### 3E0B · If ACC ≥ value

Operands (1 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A23EC`

1. Value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e0b)

<a id="op-3e0c"></a>

### 3E0C · If ACC < value

Operands (1 word(s)) · Kind: opener · Confirmed in code · Handler `0x800A2414`

1. Value — `value`: Number

<a id="op-3e0d"></a>

### 3E0D · ACC = kills[character]

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A228C`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e0d)

<a id="op-3e0e"></a>

### 3E0E · variable = (variable + 1) mod 4

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A2198`

1. Variable — `flag`: 2-bit variable 0–199

<a id="op-3e0f"></a>

### 3E0F · variable = (variable − 1) mod 4 (3 when ≤ 0)

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A220C`

1. Variable — `flag`: 2-bit variable 0–199

<a id="op-3e10"></a>

### 3E10 · If the choice was option 1

No operands · Kind: opener · Confirmed in code · Handler `0x800A2394`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e10)

<a id="op-3e11"></a>

### 3E11 · If the choice was option 2

No operands · Kind: opener · Confirmed in code · Handler `0x800A23A4`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e11)

<a id="op-3e12"></a>

### 3E12 · If the choice was option 3

No operands · Kind: opener · Confirmed in code · Handler `0x800A23B4`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e12)

<a id="op-3e13"></a>

### 3E13 · variable = value

Operands (2 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A2158`

1. Variable — `flag`: 2-bit variable 0–199
2. Value — `value`: Number

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e13)

<a id="op-3e14"></a>

### 3E14 · ACC = value

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A2138`

1. Value — `value`: Number

<a id="op-3e15"></a>

### 3E15 · ACC = number of units on a side

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A24FC`

1. Side — `faction`: Side index 0 player / 1 enemy / 2 third party

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e15)

<a id="op-3e16"></a>

### 3E16 · ACC = the other character in the battle (not the given one)

Operands (1 word(s)) · Kind: statement · Structure confirmed · Handler `0x800A2478`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e16)

<a id="op-3e17"></a>

### 3E17 · ACC = engine+0x9B6 (signed byte)

No operands · Kind: statement · Structure confirmed · Handler `0x800A2460`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e17)

<a id="op-3e18"></a>

### 3E18 · ACC = current scene

No operands · Kind: statement · Confirmed in code · Handler `0x800A2570`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e18)

<a id="op-3e19"></a>

### 3E19 · engine+0x998 = engine+0x9A8

No operands · Kind: statement · Structure confirmed · Handler `0x800A2590`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e19)

<a id="op-3e1a"></a>

### 3E1A · ACC = engine+0x998

No operands · Kind: statement · Structure confirmed · Handler `0x800A25A0`

<a id="op-3e1b"></a>

### 3E1B · ACC = character presence (0 absent / 1 on the map / 3 withdrawn)

Operands (1 word(s)) · Kind: statement · Confirmed in code · Handler `0x800A1F7C`

1. Character — `actor`: Character number (base:actors)

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e1b)

<a id="op-3e1c"></a>

### 3E1C · Unconditional block start (always true)

No operands · Kind: opener · Confirmed in code · Handler `0x800A2584`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e1c)

<a id="op-3e1d"></a>

### 3E1D · End of conditional block

No operands · Kind: block end · Confirmed in code · Handler `0x800A1D60`

[Notes (Chinese)](../../../zh/docs/script-commands/#op-3e1d)

## Context markers

At a marker the script carries on if it matches the current context, otherwise it skips to the next matching marker or the end of the event.

| Marker | Name | Matches |
|---|---|---|
| `3DD0` | Common section (any context) | always |
| `3DD1` | Arklight Blue route | current hero |
| `3DD2` | Selain Meneth route | current hero |
| `3DD3` | Brad Skywind route | current hero |
| `3DD4` | Manami Hamill route | current hero |
| `3DD5` | Real-type hero (3DD1/3DD2) | hero category |
| `3DD6` | Super-type hero (3DD3/3DD4) | hero category |
| `3DD7` | Male hero (3DD1/3DD3) | hero category |
| `3DD8` | Female hero (3DD2/3DD4) | hero category |
| `3DD9` | Choice option 1 | choice result |
| `3DDA` | Choice option 2 | choice result |
| `3DDB` | Choice option 3 | choice result |

## Operand types

An operand’s type gives its range; types not listed here are described by the operand names.

| Type | Meaning |
|---|---|
| `actor` | Character number (base:actors) |
| `text` | Text number in table 0 |
| `unit` | Unit number (base:units) |
| `flag` | 2-bit variable 0–199 |
| `scene` | Scene |
| `group` | Deployment record group |
| `position` | High byte x / low byte y; 0x40–0x44 relative to the position 3D54 remembered |
| `x` | Square x |
| `y` | Square y |
| `count` | Count |
| `frames` | Frames |
| `se` | Sound effect number |
| `bgm` | Track number |
| `event_slot_type1` | Registration index of a type 1 event in this scene |
| `event_slot_type8` | Registration index of a type 8 event in this scene |
| `actor_or_group` | Below 500 a character number, from 500 a group (value − 500) |
| `bool` | Switch |
| `value` | Number |
| `side` | Phase 1 player / 2 enemy / 3 third party (4 = any) |
| `deploy_side` | Deployment side 0/1/2 (3 → 0, 4 → 2) |
| `raw` | Not yet understood |
| `faction` | Side index 0 player / 1 enemy / 2 third party |
| `map` | Map number (base:map_assets; written to 8010F5EE) |

## Variables

200 variables of 2 bits each (values 0–3), stored at `0x8015E818`, read and written by the conditions and kept across stages.

## Deployment records (28 bytes)

A stage’s deployment records place its units, 14 halfwords each, ending at group 999. “Mini stage field” is the matching field name in a stage’s `deployments`.

| Offset | Bytes | Name | Mini stage field | Confidence |
|---|---|---|---|---|
| `+0` | 2 | Group | `group` | Confirmed in code |
| `+2` | 2 | x | `x` | Confirmed in code |
| `+4` | 2 | y | `y` | Confirmed in code |
| `+6` | 2 | Pilot | `actor` | Confirmed in code |
| `+8` | 1 | Unexplained byte | `byte8` | Not yet understood |
| `+9` | 1 | Level offset | `level_offset` | Confirmed in code |
| `+10` | 2 | Unit | `unit` | Confirmed in code |
| `+12` | 2 | Upgrade index | `upgrade` | Confirmed in code |
| `+14` | 6 | Not yet understood | `raw14, raw16, raw18` | Not yet understood |
| `+20` | 2 | Side | `faction` | Confirmed in code |
| `+22` | 2 | Behaviour flags | `behavior` | Structure confirmed |
| `+24` | 2 | Extra value | `extra` | Structure confirmed |
| `+26` | 2 | Not yet understood | `raw26` | Not yet understood |
