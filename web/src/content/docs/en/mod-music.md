---
title: Music mods
summary: Replace any of the game's 49 songs with your own music, set loops, volume and titles, and choose a pilot's or unit's battle theme. Made with the web tool or by hand.
section: mods
order: 2
updated: 2026-10-26
---

A music mod replaces one of the original’s songs with an audio file. Everywhere the game plays that song follows: battle themes, stage music, story scenes, the title and the Sound Test. You can also choose which song a pilot or unit fights to.

For installing and managing packages, see [Mod packages: installing and making them](../mods/).

## Making one with the web tool

The [BGM MOD maker](/en/mods/bgm/) does everything in your browser; **no audio is uploaded anywhere**:

1. Fill in the package details: ID, version, name, authors and license.
2. In the list of 49 songs, drop an audio file on the row you want to replace. mp3, wav, flac, m4a and ogg all work; the tool converts them to the game’s format, and a file that is already Ogg Vorbis is used as it is.
3. Click “Edit” to set up the song:
   - **Loop**: loop the whole song, loop a section of it, or play it once. Drag the blue (start) and gold (end) lines on the waveform to set the section; they snap to the nearest zero crossing when you let go. “Hear the seam” plays the moment the loop wraps around.
   - **Volume (dB)**: the average level of this song and of the other replaced songs is shown beside it, so you can match them.
   - **Title**: the name shown in the Sound Test, in each language if you like.
4. If you want, choose under “Battle songs” which song a pilot or unit fights to.
5. Click “Export .srw64mod” and drag the file into the game window to install it.

To change a package later, load it back with “Open an existing .srw64mod”.

## In the game

- In the Sound Test, a replaced song is marked “MOD”, with the package’s title if it gives one.
- The General page of the settings has “MOD music” on or off and its volume (25–100%); changes apply from the next song that starts.
- Karaoke Mode keeps the original songs, since the lyrics follow their timing.

## How the original picks songs

- **Battle themes**: whenever a player unit takes part, the fight plays **the player unit’s main pilot’s** theme, whoever attacks; when no player unit takes part, it plays the attacking unit’s theme.
- The original always plays a fixed song in these cases, and a mod’s battle song does not change them: Burning Gundam H (its Meikyo Shisui form), the Shuffle Alliance’s Super Mode forms, V-MAX, and any fight with Val-A, Avie-Lu or J-Ren in it. Those fixed songs can still be replaced themselves.
- After a battle the stage music does not come back: the battle theme plays on until the next phase starts. Keep that in mind when you set a loop.
- Stage music starts again at the beginning of every phase.
- The short tune for enemy reinforcements (song 38) plays once by default, and then the game goes back to the stage music by itself.

## Making one by hand

These are the files the web tool writes.

```text
yourname.bgm/
  mod.json
  audio/
    music/<song>.ogg
    music.json        optional
```

- Each file is named by its **song number** (decimal, no leading zeros): `7.ogg` replaces song 7, “メインテーマ”. The [song table](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/audio-mods/songs.csv) lists all 49 songs with their numbers, titles and where they play.
- The format must be **Ogg Vorbis**, at any sample rate, mono or stereo. Vorbis alone keeps loop points exact to the sample: an MP3 gains padding at its start and end, which leaves a gap where it loops. Convert other formats first, for example:

  ```bash
  ffmpeg -i in.wav -c:a libvorbis -q:a 6 out.ogg
  ```

`music.json` is optional. With only the `.ogg` files, each song loops whole at its own volume under its original title. To fine-tune, write:

```json
{
  "schema": "srw64.music.v1",
  "tracks": {
    "7":  { "loop": [441000, 5292000], "gain": -2.0,
            "title": { "zh-Hans": "…", "en": "…", "ja": "…" } },
    "38": { "loop": false }
  },
  "battle": {
    "pilot": { "36": 7 },
    "unit":  { "97": 12 }
  }
}
```

| Field | Meaning |
| --- | --- |
| `tracks.<song>.loop` | `[start, end]` in **samples** (not seconds); at the end it jumps back to the start. `false` plays the song once, `true` loops it whole. Without it, the file’s `LOOPSTART`/`LOOPLENGTH` tags are used, and without those the whole song loops |
| `tracks.<song>.gain` | A volume change in dB; negative is quieter |
| `tracks.<song>.title` | The title shown in the Sound Test, per language |
| `battle.pilot.<pilot>` | The song (by number) this pilot fights to as a player unit’s main pilot |
| `battle.unit.<unit>` | The song this unit fights to as the attacker when no player unit takes part |

Pilot and unit numbers are in the [pilot table](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/audio-mods/pilots.csv) and the [unit table](https://github.com/dyzz/srw64-recomp/blob/main/docs/guide/audio-mods/units.csv), which also list each one’s original battle theme. When several packages set the same song or the same pilot, the package higher in the list wins.

### Letting an AI do it

The repository’s [`docs/guide/audio-mods/`](https://github.com/dyzz/srw64-recomp/tree/main/docs/guide/audio-mods) folder holds the full format description and every table (its description is in Chinese). Hand the whole folder to an AI assistant and it can rename and convert your files and write `mod.json` and `music.json` for you.

## Coming later

The format for battle voices (one audio file per battle line) is settled, and the tables list the record numbers of all 11,548 battle lines, but the game does not support them yet: voice files in a package have no effect for now.

## Copyright

Use only music you have the right to use and to pass on. A package made from the original anime songs is for your own use and cannot be submitted to the official mod list.
