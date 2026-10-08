---
title: "What recompilation is: how Marchwind 64 is built"
date: 2026-10-08
summary: "Marchwind 64 is neither an emulator nor a remake: the original program is translated, whole, into code that computers and phones run directly. This post explains static recompilation and how the project fits together."
---

People often ask whether this is built on an N64 emulator, or whether the game has been remade. It is neither. Marchwind 64 uses **static recompilation** (recomp): the program on the original cartridge is translated function by function into C, and modern compilers turn that into native programs for Windows, macOS, Linux and Android.

## Emulation, remakes and recompilation

- An **emulator** pretends your computer is an N64. While the game runs, it interprets the original MIPS instructions one by one and imitates the N64's processor, graphics chip and audio chip. The game is untouched, but every instruction goes through that layer.
- A **remake** writes a new game after the original: graphics, rules and story are all implemented again, and it is hard to match the original exactly.
- **Recompilation** sits in between. The game logic is the original's own, translated ahead of time into native code. Battle calculations, enemy AI, event scripts and the save format behave exactly as in the original; nothing has to imitate the processor at run time, and new features can be attached at the edges of the original functions.

An analogy: an emulator is a live interpreter translating each sentence as it is spoken; recompilation translates the whole book before it is printed; a remake writes a new book from the same story.

## From cartridge to native program

1. **Find the code.** The original program is in 18 parts: one stays in memory, and 17 are loaded from the cartridge as needed (the title, the maps, battles, the intermission screens and so on, some sharing the same memory). We worked out where each part sits on the cartridge and where it runs: 3,407 functions in all.
2. **Translate to C.** The open-source [N64Recomp](https://github.com/N64Recomp/N64Recomp) turns each function's MIPS instructions into equivalent C, keeping the original memory layout and arithmetic.
3. **Replace the system layer.** The original program relies on the N64's system library (threads, message queues, timers, controllers, saves). [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) implements those with the computer's threads and files, so the translated code never needs to know it is not on an N64.
4. **Graphics and sound.** The N64 draws through "display lists" sent to its graphics chip. The game still builds them, and [RT64](https://github.com/rt64/rt64) draws them on modern GPUs with Direct3D 12, Vulkan or Metal; widescreen and HD texture replacement happen there too. The audio program that ran on the audio chip is recompiled into native code as well.
5. **Compile.** The generated C and our own code are compiled together for each platform.

The game's images, sounds and text are still read from your own ROM, which is why you need your own Japanese ROM.

## Building on top of the original

Because the original functions are now ordinary C functions, we can take over or wrap specific ones; there are 134 such places today. They are used for:

- **Native screens.** Dialogue, the intermission menus, upgrades, parts, saves and settings are redrawn with [RmlUi](https://github.com/mikke89/RmlUi), in Chinese, English and Japanese, with high-resolution text and touch support. The game's state is still managed by the original code, and the original screens are a switch away.
- **Translation.** All dialogue ships as plain text files, and interface and data text (names of people, units, weapons, spirits) comes from term tables, swapped in by language at run time; players can edit them too.
- **HD art.** Portraits, backgrounds, maps, unit poses and battle animations are replaced with high-resolution art as they are drawn, without touching the game logic.
- **Quality of life.** Save slots and autosave, fast-forward and skip, battle animation abort, remappable controls, filters and bezels, and fixes for the original's bugs that can be switched on and off.
- **Development tools.** A built-in debug interface lets AI tools read the game's state, press buttons and take screenshots, for automated tests and reproducing problems.

Our own part is about 36,000 lines of C++ (runtime, screens, translation, HD, platform support), plus about 40,000 lines of Python tools for script analysis, the translation pipeline, art packaging, tests and releases.

## One codebase, five platforms

macOS, Windows, Linux, the Steam Deck and Android share one codebase, drawing with Metal, Direct3D 12 and Vulkan. Every commit is built automatically on GitHub for Windows, Linux and Android.

## Thanks

Marchwind 64 would not exist without these open-source projects: [N64Recomp](https://github.com/N64Recomp/N64Recomp) and [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (recompilation and runtime), [RT64](https://github.com/rt64/rt64) (graphics), [RmlUi](https://github.com/mikke89/RmlUi) (interface) and [librashader](https://github.com/SnowflakePowered/librashader) (RetroArch filters), and the experience of earlier projects such as Zelda 64: Recompiled. The project's source is on [GitHub](https://github.com/dyzz/srw64-recomp) under GPL-3.0.
