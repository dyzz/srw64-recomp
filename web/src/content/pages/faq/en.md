---
title: FAQ
lead: Common questions about installation, controls and saves. If you cannot find an answer, open an issue on GitHub.
---

## Before you start

### Is this an emulator?

It is a native port built using static recompilation. The N64's MIPS program is translated into C, then compiled into a native Windows, macOS, Linux, Android or Switch application, with graphics rendered by [RT64](https://github.com/rt64/rt64) (by an OpenGL renderer of its own on the Switch). No BIOS, plugins or emulator configuration are needed. This approach also allows changes to the interface and additional features.

<span id="rom"></span>

### Why do I need my own ROM?

The game content is read from your own ROM at launch; this project does not include a ROM. It provides the recompilation tools and runtime, translations and interface. The original game's code, text, graphics and music belong to their respective rights holders.

Only Super Robot Wars 64, Japan Rev 0, is supported. Any byte order works (`.z64`, `.v64` or `.n64`); the game recognises and converts it. The SHA-256 of the `.z64` form is
`ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e`. Other revisions are not supported.

### Which platforms and controllers are supported?

| Platform | Requirements |
| --- | --- |
| Windows (experimental) | 64-bit Windows |
| macOS | Apple silicon, macOS 14 or later |
| Linux | x86-64, glibc 2.35 or later, a Vulkan driver |
| Steam Deck (experimental) | The Linux package; the Deck is recognised automatically |
| Android (experimental) | arm64, Android 9 or later, Vulkan 1.1; phones tested: [Android compatibility](/en/android/) |
| Nintendo Switch (experimental) | Atmosphère custom firmware and hbmenu, application mode; an NRO only, no NSP |

The CPU, graphics card and driver needed on each platform are listed under [System requirements](/en/install/#requirements) on the install page.

Keyboards and common controllers are supported, and on-screen button prompts follow the device you used last. On Android, touch controls are available when no controller is connected. Touch the screen to show the buttons needed for the current screen, labelled with their functions, such as “OK”, “Fast” or “Next unit”. You can remap keyboard and controller inputs on the Controls page in settings.

### What is different from the original?

The story is preserved, while stats, enemy behaviour and battle calculations are based on the original. The Rules page in settings includes corrections that can be turned off. When enabled, these adjust the corresponding calculations and rules.

Most additions focus on the interface, reading and controls: natively rendered settings and intermission screens, dialogue history and auto-reading, fast-forward and skip, widescreen, extra save slots, autosaves and remapping.

<span id="install"></span>

## Installing

<span id="rom-location"></span>

### Where do I put the ROM?

- **Windows**: rename your ROM to `rom.z64` and put it in the unzipped `Marchwind64-windows-x64` folder, then double-click `Marchwind64.exe`. `%LOCALAPPDATA%\SRW64Recomp\rom.z64` works too. Windows hides file extensions by default, so the file can end up as `rom.z64.z64`: turn on “File name extensions” under View in File Explorer first.
- **Linux and Steam Deck**: rename it to `rom.z64` next to `marchwind64.sh`, or put it at `~/.local/share/srw64-recomp/rom.z64`.
- **macOS and Android**: no renaming; choose the ROM in the window that opens on first launch.
- **Nintendo Switch**: rename it `rom.z64` and put it in `/switch/Marchwind64/` on the SD card. The first launch imports it on the console in about half a minute.

Unzip the ROM first if it is in an archive. A `.n64` or `.v64` dump can be named `rom.n64` or `rom.v64`. See [Install](/en/install/) for the full steps.

### What if Windows shows a security prompt?

Choose “More info” → “Run anyway”.

### How does portable mode work on Windows?

Create an empty folder named `user-data` next to `Marchwind64.exe`, and saves and settings are kept in it (the first time, settings and saves already in `%LOCALAPPDATA%\SRW64Recomp` are copied in, and a normal exit copies them back). The whole game folder, ROM included, can live on a USB stick or external drive and keeps working under another drive letter or on another computer.

### Why hold R on the Switch?

hbmenu opened from the Album has too little memory, and the game tells you to hold R instead.

### How do I update on the Switch?

Delete the old `fonts`, `dialogue`, `stages` and `splash` folders and extract the new version; saves, settings and the `game` folder stay, and the new version imports once more on its first launch. A new HD pack replaces the whole `hd` folder.

The Switch version has no in-game update notice: watch the [install page](/en/install/) or GitHub for new versions.

### Is the stutter on the Switch normal?

The Switch version still needs optimizing, and you will see some stutter while playing.

## Saves

<span id="saves"></span>

### Where are my saves? Do they work with emulators?

Saves are in the `saves` folder of the user directory:

- **Windows**: `%LOCALAPPDATA%\SRW64Recomp\saves`; type it in File Explorer’s address bar and press Enter. In portable mode (a `user-data` folder next to `Marchwind64.exe`), `user-data\saves` in the game’s folder.
- **macOS**: `~/Library/Application Support/SRW64Recomp/saves`; press ⇧⌘G in Finder and paste it.
- **Linux and Steam Deck**: `~/.local/share/srw64-recomp/saves`.
- **Android**: open “Marchwind64” in the system Files app, then `saves`.
- **Nintendo Switch**: `/switch/Marchwind64/saves` on the SD card; copy it off with a card reader or ftpd.

`cartridge.sram` is the cartridge save (slots 1 and 2), `slots/` holds slot 3 onwards and `auto/` the autosaves. To back up, copy the whole `saves` folder; to restore, quit the game and put it back. The same moves your saves to another computer or platform.

The cartridge save `saves/cartridge.sram` is byte-for-byte compatible with an ares `.ram` file, so you can copy saves between the two. For other emulators, use the Saves page in settings. “Export for emulators” converts slots 1 and 2 to the formats used by ares, Project64, mupen64plus and RetroArch. “Import from emulators” reads saves placed in the `import` folder and adds them as new slots, preserving your existing saves.

### Can I save state at any moment?

No. The game does not run in an emulator, so there are no emulator-style save states that save and load at any moment. You save the way the original does, and autosaves have been added:

- They are on by default, and save after each map, before each sortie and at the start of each of your turns.
- By default 3 intermission autosaves and 5 turn autosaves are kept, and the oldest are deleted once there are more. Change how many, or turn autosaves off, on the Saves page in settings.
- Load an autosave from the Load screen on the title screen. Loading a turn autosave starts that turn again.

<span id="hd"></span>

## HD pack

### What is the HD pack?

The HD pack is an optional, separate download that replaces the game's graphics. Without it, the game uses the original graphics. Once installed, you can switch between original and HD graphics at any time in game.

The images are mainly made in two ways, with their sources listed in the pack and release notes:

- **AI-generated**: portraits, intermission backgrounds, space objects and the title image are generated by Alibaba Cloud’s Qwen image models using the original graphics as reference. The story world map and the tactical map base images are drawn by OpenAI’s image model, also using the original graphics as reference.
- **Upscaled from the original**: unit art and map unit icons are redrawn from the original pixel art with local upscaling models. The tactical maps’ palette-index maps are upscaled from the original map pixels.

Frames are drawn by code. The BANPRESTO logo, GAME OVER and window borders are generated from your ROM at runtime and are not included in the pack.

### How do I install the HD pack on Android and the Switch?

On Android, import it in the app: long-press the app icon and choose “Import HD pack”, or open the downloaded zip with Marchwind64. The Switch takes a Switch HD pack of its own, extracted to the SD card's root; the computers' pack does not work there.

### Can the HD pack sit next to the game?

Yes: next to `Marchwind64.app`, `marchwind64.sh` or `Marchwind64.exe`.If both the game’s folder and the user data directory have one, the user data directory’s is used.

On macOS this works once the app has been moved in Finder (into Applications, say).

### How do I switch between original and HD?

At any time, under “Images” on the General page of the settings window, or with the shortcut (by default 4 or F6 on the keyboard, or pressing the right stick on a controller); the Windows and macOS menu bars also have “HD Pictures”. It takes effect at once and does not touch your saves. The choice is not saved: the game always starts in HD.

In HD you can keep the original unit icons on the map: “Map unit icons” on the General page. This one is remembered.

### How does the HD pack update?

The HD pack has versions of its own and only gets a new one when its contents change, so you do not need to download it again with every app update. The game’s update check says when there is a new one; replace the whole `hd` folder when you update.

## Language and controls

### How do I change the language?

Choose Chinese, English, Japanese or Korean under Language in settings (the Korean translation is by the community translator [테오 (jung5441)](https://github.com/jung5441)). There is also an in-game shortcut; its button depends on your platform and is listed on the Controls page of the settings. Dialogue, menus and data text switch together, and your choice is saved for the next launch.

### Controls and quitting on the Steam Deck

The game recognises the Deck and uses Deck-friendly default controls, which you can remap in the settings.

Quitting through the Steam menu saves the game before exiting.

## When something goes wrong

<span id="gpu"></span>

### The game stops on one screen, or the window is "Not responding"?

Most often the graphics driver is too old. Update it to at least:

- **NVIDIA**: newer than 475.14.
- **AMD**: a driver released in 2019 or later.
- **Intel**: the latest driver.
- **Linux**: your distribution's latest Mesa or NVIDIA driver. On the **Steam Deck**, keep SteamOS up to date.

Get the driver from the GPU maker's website; the ones Windows Update installs are often old. A laptop with two GPUs needs both updated. If it still stops, open the game again, export a problem report under Options → Report and attach it to a [GitHub issue](https://github.com/dyzz/srw64-recomp/issues).

### How do I report a problem?

Open an issue on [GitHub](https://github.com/dyzz/srw64-recomp/issues) using the form, with your version, platform and the steps leading up to the problem so it can be reproduced. In the game, Options → Report copies your platform info and exports a problem report (version, system, graphics, settings and the logs of the last few runs; no saves and no ROM): please attach it, and screenshots if you have them. For story lines and Library names, suggest a correction next to the line on the site's [Story](/en/story/) and [Library](/en/library/) pages.
