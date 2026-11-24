#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "recomp.h"

// The Library's map movies (docs/native/library.md, 动画): the twelve combining, changing and
// Aura Road sequences the tactical map plays (802176A8, ROM table 0x106F20; docs/data/
// battle-graphics.md). The original player and its step functions run as they are, copied by
// tools/recomp/toolchain/generate_cpu.py (funcs_movie.c) and called over a private copy of
// RDRAM holding the tactical overlay's data; the host stands in for what they call outside
// themselves (the sprite engine's load, release, hide and state, the screen wipe, the music
// and the sound effects), and draws the sprites they leave from the player's ROM, or the art
// pack's HD frames. Nothing of the running game is touched but its music and sound effects,
// which play through the original's own calls on the game thread.
namespace srw64::map_movie {
constexpr unsigned count = 12;
enum class Kind { combine, aura_road, change };
struct Info {
    Kind kind;
    int unit;                   // the unit whose work groups it in the Library (-1: none)
    bool used;                  // 2 has no caller in the game
    std::array<int, 3> units;   // the units it shows, -1 past the last
};
const Info& info(unsigned movie);

// The window the movies play in, in the original's 320 x 240 picture: x0, y0, x1, y1, 112 x 96
// about the middle. Each movie's backdrop fills it (its scene is 128 wide, the last 16 columns
// clear); the wipe's bars (priority 0x83, drawn after the sprites) cover the rows above and
// below it.
constexpr float window[4] = {104, 72, 216, 168};
// One sprite as the frame shows it, in the original's 320 x 240 picture.
struct Draw {
    uint16_t scene = 0, atlas = 0, palette = 0;
    uint8_t frame = 0;
    float rect[4]{};            // x0, y0, x1, y1
    float alpha = 1;
    bool flip_x = false, flip_y = false;
    float rotation = 0;         // degrees, clockwise on the screen, about `pivot`
    float pivot[2]{};
};
struct Picture {
    uint64_t serial = 0;        // moves with every new picture
    int movie = -1;
    unsigned tick = 0, length = 0;
    bool playing = false, paused = false, finished = false, looping = false, failed = false;
    std::vector<Draw> draws;    // back to front
    std::vector<std::array<float, 4>> black;   // the wipe's bars: x0, y0, x1, y1
};

// Window thread. play() starts a movie from its first frame (with its music); pause() holds
// the picture (and stops the music: it cannot go on from the middle); step() moves a paused
// movie one tick on or back (it is run again silently from the start to go back); stop()
// ends it and gives the title its song back.
void play(unsigned movie);
void pause(bool paused);
void step(int ticks);
void set_loop(bool loop);
void stop();
Picture picture();
// A still for the page before it plays: the movie run silently to `tick` (past its end: its
// last), with no music or sound.
Picture preview(unsigned movie, unsigned tick);
// How many ticks (1/30 s) a movie runs, from a silent run of it; 0 if it failed.
unsigned length(unsigned movie);
// How many scenes a movie's table names.
unsigned scenes(unsigned movie);

// A scene frame's original pixels: straight RGBA, `origin` the frame's top left corner in the
// scene's coordinates (where the HD frame goes too).
struct Image {
    int width = 0, height = 0;
    int origin[2]{};
    std::vector<uint8_t> rgba;
};
std::optional<Image> frame_image(uint16_t scene, uint16_t atlas, uint16_t palette, uint8_t frame);

// For the debug interface's status: the movie playing, its tick, and the songs and sound
// effects (tick, number) it has played through the game since it started.
struct Played { int movie = -1; unsigned tick = 0; bool playing = false; std::vector<int> songs; std::vector<std::pair<unsigned, int>> sounds; };
Played played();

// Game thread, every frame (game_hooks.cpp): ticks a playing movie at 30 per second and
// plays what it asks for.
void game_frame(uint8_t* ram, recomp_context* ctx);
}
