<p align="center">
  <img src="web/public/brand/title-en.webp" alt="MARCHWIND64" width="640">
</p>

<p align="center"><b>Super Robot Wars 64 — a native recompilation of the 1999 N64 game.</b></p>

<p align="center">
  English · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://srw64.dreamquest.club/en/">Website</a> ·
  <a href="https://srw64.dreamquest.club/en/install/">Download and install</a> ·
  <a href="https://srw64.dreamquest.club/en/faq/">FAQ</a> ·
  <a href="https://srw64.dreamquest.club/en/story/">Story</a> ·
  <a href="https://srw64.dreamquest.club/en/library/">Library</a> ·
  <a href="https://srw64.dreamquest.club/en/guide/">Guide</a>
</p>

Marchwind 64 is an unofficial native recompilation project for Super Robot Wars 64.
It runs natively on Windows, macOS, Linux, Steam Deck and Android, with full English
and Chinese translations and widescreen support, plus an optional HD art pack.

> **Requires your own Japanese ROM** (Super Robot Taisen 64, Rev 0). Neither this
> repository nor the downloads contain any game data. See
> [About ROMs](https://srw64.dreamquest.club/en/faq/#rom).

| Native | EN · ZH · JA | HD and widescreen | Original bug fixes |
| --- | --- | --- | --- |
| Install, add your ROM and play | Full translations, switchable in game | Switch between original and HD art at any time | Enabled by default, with individual toggles |

## Original and HD graphics

| Original · 4:3 | MARCHWIND 64 · HD widescreen |
| --- | --- |
| ![Original 4:3 view](web/public/media/compare-original-en.webp) | ![HD widescreen view](web/public/media/compare-hd-en.webp) |

Left: the original 4:3 view, with N64-resolution portraits and maps. Right: Marchwind 64
with the HD pack, showing redrawn portraits and world map in 16:9. Switch between
original and HD art at any time in game; both use the same saves.

## Interface and control updates

Built on the N64 original, with updated screens, full translations and control
improvements, plus individually selectable bug fixes. All images below are in-game
screenshots.

### Dialogue reader

| | |
| --- | --- |
| ![Auto-play](web/public/media/auto-en.webp) | ![Dialogue history](web/public/media/history-en.webp) |

All dialogue is re-typeset in a high-resolution font. Each line runs continuously and
is paged to fit the text size, avoiding page breaks mid-sentence. Earlier lines can be
reviewed at any time, and auto-play, fast-forward and skip each have their own
control, with the current reading mode and controls shown along the bottom of the
screen.

- Review the last 256 lines, coloured by speaker
- Four auto-play speeds; text size from 10 to 18
- Fast-forward and skip work as in modern Super Robot Wars games: hold to fast-forward
  and let go to stop; skip runs through the whole scene to the next choice or battle,
  with the same result as reading every line
- Switch language or check the original Japanese at any time; the current line is
  re-typeset at once
- Dialogue is stored as plain text files you can edit yourself

### Modern SRW screens and controls

| | |
| --- | --- |
| ![Pre-battle screen](web/public/media/prebattle-en.webp) | ![Jumping to the farthest square](web/public/media/move-jump.webp) |

The pre-battle screen is redesigned in the style of modern Super Robot Wars games:
damage, hit and critical rates for both sides appear side by side with shield and
barrier effects, and spirits, weapon selection and the battle animation toggle are
grouped below. The tactical map gains the shortcuts modern entries have too.

- While choosing a destination, jump straight to the farthest reachable squares
- L1/R1 step through your units, L2/R2 through the enemy’s
- Stop a battle animation at any time; the result is calculated normally
- The original screen is also available in settings

### In-game settings

![Settings](web/public/media/settings-en.webp)

Adjust language, original or HD graphics, aspect ratio, full screen and interface size
during play. You can also toggle individual rule fixes and remap keyboard and
controller inputs.

- Open them at any time in game, or from the button at the bottom right of the title screen
- Three interface sizes: standard, large and largest

### RetroArch filters and bezels

![RetroArch bezel with the crt-lottes filter](web/public/media/filters-tv-crt.webp)

Use RetroArch slang shader presets (.slangp) directly for CRT scanlines, NTSC colour and
more. At 4:3 you can also add a RetroArch bezel; its transparent window lines up with the
picture by itself. Both are in Options → General.

- Nine common presets included, such as crt-lottes, crt-geom, zfast-crt (for handhelds),
  ntsc-adaptive and xbrz-freescale
- Finds every slang shader of an installed RetroArch; drop your own presets and bezels in
  the data folder
- Works on the original 240 lines (closest to a CRT), 480, 960, or the window’s resolution
- Only the game picture and dialogue are filtered; the options window, notices and the
  bezel stay sharp
- Filters run on macOS and Linux/Steam Deck; not on Windows yet

<sub>Bezel: tv-integer from RetroArch’s own overlays (libretro/common-overlays, CC BY 4.0).
Filter: crt-lottes at 480 lines. The game ships with no bezels.</sub>

### Library: 353 units, 293 characters

![Library](web/public/media/library-en.webp)

Organised by series, with unit stats, terrain ratings, weapons and their fully upgraded
power, upgrade caps, and each pilot’s growth, spirits and skills. All entries come
directly from the game data.

- Open it from the title screen or during play
- The same [Library](https://srw64.dreamquest.club/en/library/) is on the website

### Battle Viewer

![Battle Viewer](web/public/media/viewer-en.webp)

Choose the attacking and defending units and pilots, set the weapon, the defender’s
reaction, the damage and the scene, then play the full battle animation.

- View weapon animations for every unit
- Plays the attacker’s theme, as in the original

## Battles in HD

High-resolution art for units, cut-ins, portraits and backgrounds, with the original
animation timing and camera work.

| | |
| --- | --- |
| ![Cut-in](web/public/media/cutin-en.webp) | ![Battle](web/public/media/spin-en.webp) |
| ![Beam attack](web/public/media/beam-en.webp) | ![Battle](web/public/media/sekiha-en.webp) |

## Bug fixes and optional rules

Each rule has its own toggle and takes effect immediately, using the same save format.
Select “Turn all off” to use the original rules.

**Original bug fixes** — on by default. Corrections to calculation errors and omissions
in the original.

- ESP: hit and evade use the actual skill level (the original always used 64)
- Holy Warrior: evade uses the actual level (the original always used 32)
- Limit: hit and evade, each combined with mobility, are capped by the unit’s limit
- Potential: corrected HP bands, with no bonus at full HP
- Potential: hit and evade bonuses halved, criticals unchanged
- Machine swap: the 3 weapons omitted from the table also retain their upgrades
- Holy Warrior: Hyper Aura Slash grows stronger with the level

**Convenience and difficulty** — off by default. Optional adjustments to upgrades,
funds and battle difficulty.

- Raised upgrade cap: every unit can reach 15 levels
- Departure refund: returns upgrade funds when a unit leaves as part of the story
- Parts carry over: enhancement parts move with the pilot to the new machine
- Boss dummies: halve their uses or disable them

**Graphics and interface** — new or original. Choose the new or original layout for
each screen.

- Pre-battle screen: new, HD original or original
- Intermission, hero selection and title menu: new or original
- Art: HD or original, switchable at any time
- Aspect ratio: fill the screen or keep 4:3
- Battle animations and autosaves: individual toggles

## More features

- **Runs natively.** The N64 program is recompiled into native code, with graphics
  rendered by RT64. After installation, add your ROM following the setup instructions
  to start playing.
- **Full text in three languages.** Full English and Chinese translations alongside
  the original Japanese. Character, unit and weapon names follow common usage in each
  language.
- **Extra save slots and autosaves.** More save slots than the original, with
  autosaves at key points. Export cartridge saves for use with ares, Project64,
  RetroArch and other emulators.
- **Widescreen without stretching.** Supports screen ratios from 4:3 to 16:9. The
  original view stays centred while the scenery extends to the sides, filling the
  screen without stretching the image.
- **Handhelds, phones, controllers.** Automatically detects Steam Deck and uses its
  button layout with a large interface by default. On Android, touch controls show the
  buttons needed for the current screen, labelled with their functions, such as “OK”,
  “Fast” and “Next unit”.
- **Optional HD art pack.** The HD art pack is a separate download that works on every
  platform. The game uses the original pixel art without it; once installed, you can
  switch between the two at any time in game.

## Platforms

| Platform | Requirements |
| --- | --- |
| Windows (experimental) | 64-bit Windows |
| macOS | Apple silicon, macOS 14 or later |
| Linux | x86-64, glibc 2.35 or later, Vulkan |
| Steam Deck (experimental) | Add it to Steam with the script, then launch from Game Mode |
| Android (experimental) | arm64, Android 9 or later, touch controls |

Step-by-step instructions for each platform are on the
[install page](https://srw64.dreamquest.club/en/install/).

## Help improve the translation

Browse the [full script by stage](https://srw64.dreamquest.club/en/story/), with the
Japanese original and translation side by side. Suggest changes on individual lines to
help resolve awkward wording or inconsistent names; the outcome of each suggestion is
public. Bugs go to [GitHub Issues](https://github.com/dyzz/srw64-recomp/issues).

## Building from source

The repository contains no ROM, saves, extracted game data, generated game code, HD art
or prebuilt application. Put your own Japanese Rev 0 ROM in the repository root as
`rom.z64` (its identity is in [provenance](docs/guide/provenance.md)).

On macOS (Apple silicon) with Python 3.11+ and the Xcode command line tools:

```sh
brew install python cmake ninja sdl2 freetype harfbuzz icu4c
make                                                  # toolchain, generated game code and the host
scripts/Play\ SRW64\ Native.command --language en     # play
```

- [Development guide](docs/guide/native-development.md): build steps, checks and module boundaries
- [Linux and Steam Deck build](docs/guide/linux-build.md) · [Releases](docs/guide/release.md)
- [Debug interface and MCP](docs/guide/debug-interface.md): drive the game from the command line or an AI agent
- [Technical documentation index](docs/README.md) (mostly in Chinese) · [Contributing](CONTRIBUTING.md)

## Credits

Built on [N64Recomp](https://github.com/N64Recomp/N64Recomp),
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) and
[RT64](https://github.com/rt64/rt64); further sources and pinned revisions are listed in
[provenance](docs/guide/provenance.md). The interface and dialogue use the HarmonyOS Sans
fonts, distributed unmodified under their licence (`content/fonts`), and button icons
adapted from Yukari “Shinmera” Hafner’s PromptFont under the SIL Open Font License.

This is an unofficial fan project. Super Robot Wars and all related characters, mechanics
and trademarks belong to their respective rights holders.

## License

The project’s own source code and tools are licensed under the
[GNU General Public License v3.0](LICENSE) or (at your option) any later version. This
covers only what the project wrote. It does not cover Super Robot Wars 64, its ROM, or
anything taken or derived from it (game code, text, data, images, music), nor the
characters, mechanics and trademarks of their rights holders. The fonts in
`content/fonts`, the HD image pack (see its NOTICE) and third-party components keep
their own terms.
