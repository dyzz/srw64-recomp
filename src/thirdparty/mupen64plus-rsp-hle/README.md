# mupen64plus-rsp-hle (audio only)

The standard N64 audio microcode (the "audio ABI", the most common one) written as native
C, from https://github.com/mupen64plus/mupen64plus-rsp-hle at commit
4cdc3051efbae9cd615f5fea37c62e5ab15a1ded (`src/`). GPL-2.0-or-later, see `LICENSES`.

Only what the audio ABI needs is here: `alist_audio.c` (its commands), `alist.c` (ADPCM,
resampling, envelope mixing), `audio.c` (tables), `memory.c` and their headers. The rest
of the plugin (graphics, JPEG, MusyX, the other audio ABIs' command tables) is left out.

Used by `src/host/audio_hle.cpp`: the Switch's audio, a trial elsewhere.

## Changed here

`alist.c` is changed so that its samples are the game's recompiled microcode's to the bit;
every change is marked `SRW64`. The other files are upstream's.

- **RESAMPLE**: each of the four products is rounded on its own (the microcode's VMULF),
  where upstream adds them and truncates once. Eight outputs at a time, their inputs all read
  before any is written (the game puts a short voice's output over the end of its input),
  and one batch even for a count of 0.
- **MIXER**: `mulf_macf`, the microcode's VMULF by 0x7fff and VMACF, in place of an added
  truncated product.
- **ENVMIXER** (`alist_envmix_exp`): rewritten after the microcode's listing. Eight volumes
  a side, each multiplied by the rate from block to block, the first block after A_INIT a
  ramp; the saved state is the microcode's 0x50 bytes.
- **POLEF**: the gain as the microcode shifts and signs it; with A_INIT the two "last
  outputs" are what the last RESAMPLE, ENVMIXER or POLEF left in the microcode's state
  area (`state_words`), as on the console.

How they were found and are checked: `tools/recomp/probes/run_audio_hle_fuzz.sh` runs each
command with random samples through both and compares the buffers; `SRW64_AUDIO_HLE=compare`
does it in the game, task by task. Both must stay at no difference after a change here.
