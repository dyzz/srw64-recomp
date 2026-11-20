#include "map_movie.hpp"
#include "guest_memory.hpp"
#include "funcs.h"
#include "app/rom_import_codec.hpp"
#include "app/battle_art.hpp"
#include "librecomp/game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>

// The sandbox's copies of the player (generate_cpu.py write_movie_sandbox).
extern "C" {
void srw64_movie_802176A8(uint8_t* rdram, recomp_context* ctx);
void srw64_movie_8021779C(uint8_t* rdram, recomp_context* ctx);
}

namespace srw64::map_movie {
namespace {
using namespace guest;
namespace rom_import = srw64::app::rom_import;

constexpr Info infos[count] = {
    {Kind::combine, 196, true, {196, -1, -1}},     // コン・バトラーV: 3D67 0 before 3D5C 145,1; unit 196 combining
    {Kind::aura_road, 247, true, {254, -1, -1}},   // ズワァース: 3D67 1
    {Kind::aura_road, 247, false, {-1, -1, -1}},   // no caller
    {Kind::aura_road, 247, true, {-1, -1, -1}},    // トッド: 3D67 3
    {Kind::aura_road, 247, true, {-1, -1, -1}},    // ジェリル: 3D67 4
    {Kind::change, 174, true, {174, -1, -1}},      // a pilot's first change to ゲッタードラゴン (801CF2EC)
    {Kind::change, 174, true, {176, -1, -1}},      // ゲッターライガー
    {Kind::change, 174, true, {175, -1, -1}},      // ゲッターポセイドン
    {Kind::change, 171, true, {171, -1, -1}},      // ゲッター1
    {Kind::change, 171, true, {172, -1, -1}},      // ゲッター2
    {Kind::change, 171, true, {173, -1, -1}},      // ゲッター3
    {Kind::combine, 187, true, {187, 185, -1}},    // ゴッドマーズ: 3D6A
};
// The scene triplets each movie's table holds (ROM 0x106F20, battle_graphics.py MAP_MOVIES).
constexpr uint32_t movie_table = 0x106F20, movie_table_size = 0x2E0;
constexpr std::array<std::pair<uint32_t, unsigned>, count> movie_runs{{
    {0x106F20, 13}, {0x106F90, 7}, {0x106FD4, 9}, {0x107028, 9}, {0x10707C, 9}, {0x1070D0, 5},
    {0x1070EE, 5}, {0x10710C, 5}, {0x10712A, 5}, {0x107148, 5}, {0x107166, 5}, {0x107194, 18}}};

// The tactical overlay (ROM 0xAB160, at 801C2600 while a map runs) up to the next overlay's
// code; its data holds the player's tables (D_8021E410...), checked by its first state's.
constexpr uint32_t overlay_rom = 0xAB160, overlay_end = 0x107BF0, overlay_ram = 0x801C2600;
constexpr uint32_t state_table = 0x8021E6D4, first_state = 0x802175FC;
constexpr uint32_t resource_table = 0x00A20BD0;
// The sprite engine (docs/design/battle-animation-rendering.md §3.1): slot headers
// 800FFA70 + slot x 0xC4 (+0 mode, +1 in use, +4/+8/+C position, +10/+14/+18 scale,
// +1C/+20/+24 rotation), sub-records 800FFAA4 + slot x 0xC4 + sub x 0x30 (+0 in use, +1 alpha,
// +4 node, +8 kind, +C/+E/+10 scene, atlas, palette, +18 ticks left, +19 step, +1A flags,
// +1C mirrored).
constexpr uint32_t slots = 0x800FFA70, slot_size = 0xC4, subs = 0x800FFAA4, sub_size = 0x30;
constexpr unsigned slot_count = 300;
// The counter the sound effect table is read against (D_8016131C: once every two VIs).
constexpr uint32_t sound_clock = 0x8016131C;
// Nodes get addresses here, outside anything the player reads.
constexpr uint32_t node_base = 0x80700000;
constexpr uint32_t stack_top = 0x807FFF00;
constexpr float tick_seconds = 1.0f / 30;

uint32_t slot_at(unsigned slot) { return slots + slot * slot_size; }
uint32_t sub_at(unsigned slot, unsigned sub) { return subs + slot * slot_size + sub * sub_size; }
float read_float(const uint8_t* ram, uint32_t at) {
    const uint32_t bits = read(ram, at, 4);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
std::span<const uint8_t> rom() { return recomp::get_rom(); }

// Scenes as the sprite engine and the frame images read them.
struct Part { unsigned flags, s, t, w, h; int x, y; };
struct Scene {
    std::vector<uint8_t> bytes;
    unsigned steps = 0;
    std::vector<std::vector<Part>> frames;
    std::vector<std::array<int, 4>> bounds;   // per frame: x0, y0, x1, y1
};
std::mutex scene_mutex;
std::map<uint16_t, std::shared_ptr<const Scene>> scene_cache;
std::shared_ptr<const Scene> scene(uint16_t id) {
    std::lock_guard lock(scene_mutex);
    if (const auto found = scene_cache.find(id); found != scene_cache.end()) return found->second;
    std::shared_ptr<Scene> s;
    try {
        auto parsed = std::make_shared<Scene>();
        parsed->bytes = rom_import::resource(rom(), resource_table, id).bytes;
        const rom_import::Bytes data(parsed->bytes);
        const auto head = rom_import::slice(data, 0, 2);
        parsed->steps = head[0];
        const size_t table = 4 + parsed->steps * 2u;
        const unsigned first = rom_import::be16(data, table);
        if (head[1] > 2 || first <= table || (first - table) % 2) throw std::runtime_error("scene table");
        for (size_t f = 0; f < (first - table) / 2; ++f) {
            std::vector<Part> parts;
            std::array<int, 4> b{32767, 32767, -32768, -32768};
            for (size_t p = rom_import::be16(data, table + 2 * f);; p += 16) {
                const auto raw = rom_import::slice(data, p, 16);
                const unsigned flags = rom_import::be16(raw, 0);
                if (flags & 0x8000) break;
                Part a{flags, rom_import::be16(raw, 2), rom_import::be16(raw, 4), raw[6], raw[7],
                       int16_t(rom_import::be16(raw, 8)), int16_t(rom_import::be16(raw, 10))};
                b = {std::min(b[0], a.x), std::min(b[1], a.y), std::max(b[2], a.x + int(a.w)), std::max(b[3], a.y + int(a.h))};
                parts.push_back(a);
            }
            if (b[2] < b[0]) b = {0, 0, 0, 0};
            parsed->frames.push_back(std::move(parts));
            parsed->bounds.push_back(b);
        }
        s = std::move(parsed);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SRW64_MAP_MOVIE scene %u: %s\n", id, error.what());
    }
    scene_cache[id] = s;
    return s;
}
// The frame a step shows (0xFF: none).
int step_frame(const Scene& s, unsigned step) {
    if (step >= s.steps || 2 + 2 * step >= s.bytes.size()) return -1;
    const unsigned frame = s.bytes[2 + 2 * step];
    return frame == 0xFF || frame >= s.frames.size() ? -1 : int(frame);
}

// One run of the player over its own RDRAM.
struct Node { unsigned slot, sub; uint8_t priority; bool hidden = false; };
struct Player {
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x800000);
    recomp_context ctx{};
    std::list<Node> nodes;                   // draw order (back to front)
    struct Wipe {
        bool on = false;
        int type = 0;
        std::array<float, 240> a{}, b{}, speed{};
    } wipe;
    uint32_t random = 0x1234567u;
    std::vector<int> songs, sounds;
    unsigned movie = 0, tick = 0;
    bool done = false, failed = false;
};
thread_local Player* current = nullptr;
std::set<std::string> warned;
void warn(const std::string& what) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    if (warned.insert(what).second) std::fprintf(stderr, "SRW64_MAP_MOVIE %s\n", what.c_str());
}

bool load(Player& p, unsigned movie) {
    const auto r = rom();
    if (r.size() < overlay_end) return false;
    for (uint32_t i = 0; i < overlay_end - overlay_rom; ++i) write8(p.ram.data(), overlay_ram + i, r[overlay_rom + i]);
    if (read(p.ram.data(), state_table, 4) != first_state) { warn("the tactical overlay is not the expected one"); return false; }
    p.ctx = {};
    p.ctx.f_odd = &p.ctx.f0.u32h;
    p.ctx.mips3_float_mode = 0;
    p.ctx.r29 = gpr(int64_t(int32_t(stack_top)));
    p.movie = movie;
    p.random = 0x1234567u + movie;
    current = &p;
    p.ctx.r4 = movie;
    srw64_movie_802176A8(p.ram.data(), &p.ctx);
    current = nullptr;
    return !p.failed;
}

// The sprite engine's step (80098738), every slot in use, once a tick.
void step_sprites(Player& p) {
    uint8_t* ram = p.ram.data();
    for (unsigned slot = 0; slot < slot_count; ++slot) {
        if (!read(ram, slot_at(slot) + 1, 1)) continue;
        for (unsigned sub = 0; sub < 3; ++sub) {
            const uint32_t at = sub_at(slot, sub);
            if (!read(ram, at, 1) || !read(ram, at + 8, 1)) continue;
            const unsigned flags = read(ram, at + 0x1A, 2);
            if (flags & 2) continue;
            const auto s = scene(uint16_t(read(ram, at + 0xC, 2)));
            if (!s || s->bytes.size() < 4 + 2 * s->steps) continue;
            const unsigned ticks = (read(ram, at + 0x18, 1) - 1) & 0xFF;
            write8(ram, at + 0x18, uint8_t(ticks));
            if (ticks) continue;
            const unsigned old = read(ram, at + 0x19, 1);
            unsigned next = (old + 1) & 0xFF;
            if (next == s->bytes[0]) {
                if (flags & 1) {   // played once: it stays on its last step, stopped
                    write8(ram, at + 0x19, uint8_t(old));
                    write16(ram, at + 0x1A, uint16_t((flags & ~1u) | 2));
                    continue;
                }
                next = s->bytes[3 + 2 * next];   // the loop step
            }
            write8(ram, at + 0x19, uint8_t(next));
            if (3 + 2 * next < s->bytes.size()) write8(ram, at + 0x18, s->bytes[3 + 2 * next]);
        }
    }
}

// The wipe's bars (80099508's lines, moved by 80098C80 / 80098D54): the rows outside 73..167.
bool bar_row(unsigned row) { return row - 73 >= 0x5F; }
void step_wipe(Player& p) {
    if (!p.wipe.on || (p.wipe.type != 6 && p.wipe.type != 7)) return;
    auto& w = p.wipe;
    for (unsigned row = 0; row < 240; ++row) {
        if (!bar_row(row)) continue;
        // In: a (the left end) runs left to 1 or b (the right end) right to 319. Out: the
        // other way about, b left to 1 or a right to 319.
        float& left_mover = w.type == 6 ? w.a[row] : w.b[row];
        float& right_mover = w.type == 6 ? w.b[row] : w.a[row];
        if (w.speed[row] < 0) left_mover = std::max(1.0f, left_mover + w.speed[row]);
        else right_mover = std::min(319.0f, right_mover + w.speed[row]);
    }
}
bool wipe_done(const Player& p) {
    const auto& w = p.wipe;
    unsigned done = 0;
    for (unsigned row = 0; row < 240; ++row) {
        if (!bar_row(row)) continue;
        const float moving = w.speed[row] < 0 ? (w.type == 6 ? w.a[row] : w.b[row]) : (w.type == 6 ? w.b[row] : w.a[row]);
        if (w.speed[row] < 0 ? moving <= 1 : moving >= 319) ++done;
    }
    return done >= 0x90;
}

// A tick: the player's step (8021779C, as the script's 3D67 runs it), the sprites', the wipe's.
void tick(Player& p) {
    if (p.done || p.failed) return;
    uint8_t* ram = p.ram.data();
    write32(ram, sound_clock, read(ram, sound_clock, 4) + 1);
    current = &p;
    srw64_movie_8021779C(ram, &p.ctx);
    current = nullptr;
    if (uint32_t(p.ctx.r2) == 1) p.done = true;
    step_sprites(p);
    step_wipe(p);
    ++p.tick;
}

// What the frame shows.
void draw(const Player& p, Picture& out) {
    const uint8_t* ram = p.ram.data();
    out.draws.clear();
    out.black.clear();
    static const float half_height = 1000.0f * std::tan(25.0f * 3.14159265f / 180);   // the map's camera: eye (0, 0, 1000), fovy 50°
    for (const auto& node : p.nodes) {
        if (node.hidden) continue;
        const uint32_t head = slot_at(node.slot), at = sub_at(node.slot, node.sub);
        if (!read(ram, head + 1, 1) || !read(ram, at, 1)) continue;
        Draw d;
        d.scene = uint16_t(read(ram, at + 0xC, 2));
        d.atlas = uint16_t(read(ram, at + 0xE, 2));
        d.palette = uint16_t(read(ram, at + 0x10, 2));
        const auto s = scene(d.scene);
        if (!s) continue;
        const int frame = step_frame(*s, read(ram, at + 0x19, 1));
        if (frame < 0) continue;
        d.frame = uint8_t(frame);
        const auto& b = s->bounds[size_t(frame)];
        if (b[2] <= b[0] || b[3] <= b[1]) continue;
        const unsigned alpha = read(ram, at + 1, 1);
        d.alpha = alpha / 255.0f;
        const unsigned mode = read(ram, head, 1);
        const float x = read_float(ram, head + 4), y = read_float(ram, head + 8), z = read_float(ram, head + 0xC);
        if (mode >= 0xB && mode <= 0xD) {
            // Texture rectangles at the truncated position (800975A4 / 8009751C, mode 0xD y - z),
            // mirrored about it when +1C is set (80096CD8).
            const float ox = float(int32_t(x)), oy = float(int32_t(mode == 0xD ? y - z : y));
            if (read(ram, at + 0x1C, 1)) { d.rect[0] = ox - b[2]; d.rect[2] = ox - b[0]; d.flip_x = true; }
            else { d.rect[0] = ox + b[0]; d.rect[2] = ox + b[2]; }
            d.rect[1] = oy + b[1]; d.rect[3] = oy + b[3];
        } else if (mode == 0xE) {
            // Quads under the slot's matrix (8008261C: translate, scale, rotate) through the map's
            // perspective; the parts' y is up in the model.
            const float sx = read_float(ram, head + 0x10), sy = read_float(ram, head + 0x14);
            const float depth = 1000.0f - z;
            if (depth <= 20) continue;
            const float k = 120.0f / (depth / 1000.0f * half_height);
            float x0 = 160 + (x + sx * b[0]) * k, x1 = 160 + (x + sx * b[2]) * k;
            float y0 = 120 - (y - sy * b[1]) * k, y1 = 120 - (y - sy * b[3]) * k;
            if (x1 < x0) { std::swap(x0, x1); d.flip_x = true; }
            if (y1 < y0) { std::swap(y0, y1); d.flip_y = true; }
            d.rect[0] = x0; d.rect[1] = y0; d.rect[2] = x1; d.rect[3] = y1;
            const float rz = read_float(ram, head + 0x24);
            if (rz != 0) {
                d.rotation = -rz * 180.0f / 3.14159265f;
                d.pivot[0] = 160 + x * k; d.pivot[1] = 120 - y * k;
            }
            if (read_float(ram, head + 0x1C) != 0 || read_float(ram, head + 0x20) != 0) warn("a quad turns about x or y; drawn flat");
        } else {
            warn("sprite mode " + std::to_string(mode) + " is not drawn");
            continue;
        }
        out.draws.push_back(d);
    }
    if (p.wipe.on) {
        // The bars, as rectangles over the rows that share their ends.
        for (unsigned row = 0; row < 240;) {
            float x0 = std::min(p.wipe.a[row], p.wipe.b[row]), x1 = std::max(p.wipe.a[row], p.wipe.b[row]);
            unsigned end = row + 1;
            while (end < 240 && std::min(p.wipe.a[end], p.wipe.b[end]) == x0 && std::max(p.wipe.a[end], p.wipe.b[end]) == x1) ++end;
            if (bar_row(row) && x1 - x0 >= 1) out.black.push_back({x0, float(row), x1, float(end)});
            row = end;
        }
    }
}

// Window thread <-> game thread.
std::mutex mutex;
struct Request { int movie = -1; bool stop = false; int step = 0; };
Request request;
bool paused = false, looping = false;
Picture shown;
std::unique_ptr<Player> playing;
int title_song = -2;          // the title's song while a movie's plays (-2: none taken)
bool music_on = false;
std::chrono::steady_clock::time_point started;
unsigned ticked = 0;          // ticks played since `started`
std::map<unsigned, unsigned> lengths;
Played heard;                 // what the game played for the movie (debug status)
}

// The player's calls out of itself, on the thread that runs it.
extern "C" {
// 80098158(slot, sub, mode, priority, scene, atlas, palette, flags): a scene into a sprite,
// with a node at that priority unless the sprite has one (80097C68, 8008B4F4).
void srw64_movie_resident_80098158(uint8_t* rdram, recomp_context* ctx) {
    Player* p = current;
    if (!p) return;
    const unsigned slot = uint32_t(ctx->r4), sub = uint32_t(ctx->r5) & 0xFF, mode = uint32_t(ctx->r6) & 0xFF, priority = uint32_t(ctx->r7) & 0xFF;
    const uint32_t sp = uint32_t(ctx->r29);
    const auto arg = [&](unsigned n) { return read(rdram, sp + 0x10 + 4 * n, 4); };
    if (slot >= slot_count || sub >= 3) { p->failed = true; warn("a sprite out of range"); return; }
    const uint32_t head = slot_at(slot), at = sub_at(slot, sub);
    write8(rdram, at + 8, 1);                       // the drawer's kind (modes 0xB-0xE: 1)
    if (!read(rdram, at, 1)) {
        write8(rdram, at, 1);
        write8(rdram, at + 1, 0xFF);
        write8(rdram, head + 1, 1);
        write8(rdram, head, uint8_t(mode));
        write32(rdram, at + 4, node_base + slot * 4 + sub);
        // 8008B614 walks past every node of this priority or higher: the list runs from the
        // highest priority down, drawn in that order, a newer node after older equals.
        auto place = std::find_if(p->nodes.begin(), p->nodes.end(), [&](const Node& n) { return n.priority < priority; });
        p->nodes.insert(place, Node{slot, sub, uint8_t(priority)});
    }
    const uint16_t scene_id = uint16_t(arg(0)), atlas = uint16_t(arg(1)), palette = uint16_t(arg(2));
    write8(rdram, at + 0x19, 0);
    write8(rdram, at + 0x1C, 0);
    write16(rdram, at + 0x1A, uint16_t(arg(3)));
    write16(rdram, at + 0xE, atlas);
    write16(rdram, at + 0x10, palette);
    write16(rdram, at + 0xC, scene_id);
    write16(rdram, at + 0x12, scene_id);
    write16(rdram, at + 0x14, atlas);
    write16(rdram, at + 0x16, palette);
    const auto s = scene(scene_id);
    write8(rdram, at + 0x18, s && s->bytes.size() > 3 ? s->bytes[3] : 1);
}
// 8008B888(slot): the slot's sprites and nodes go.
void srw64_movie_resident_8008B888(uint8_t* rdram, recomp_context* ctx) {
    Player* p = current;
    if (!p) return;
    const unsigned slot = uint32_t(ctx->r4);
    if (slot >= slot_count || !read(rdram, slot_at(slot) + 1, 1)) return;
    write8(rdram, slot_at(slot) + 1, 0);
    for (unsigned sub = 0; sub < 3; ++sub) {
        write8(rdram, sub_at(slot, sub), 0);
        write8(rdram, sub_at(slot, sub) + 1, 0);
    }
    p->nodes.remove_if([&](const Node& n) { return n.slot == slot; });
}
// 8008B988(slot, sub, hide): the node's hidden bit (4).
void srw64_movie_resident_8008B988(uint8_t* rdram, recomp_context* ctx) {
    Player* p = current;
    if (!p) return;
    const unsigned slot = uint32_t(ctx->r4), sub = uint32_t(ctx->r5);
    if (slot >= slot_count || sub >= 3 || !read(rdram, sub_at(slot, sub), 1)) return;
    for (auto& n : p->nodes)
        if (n.slot == slot && n.sub == sub) n.hidden = uint32_t(ctx->r6) != 0;
}
// 800988B8(slot, sub): the sprite's playing flags (2: a played-once scene has ended).
void srw64_movie_resident_800988B8(uint8_t* rdram, recomp_context* ctx) {
    const unsigned slot = uint32_t(ctx->r4), sub = uint32_t(ctx->r5);
    ctx->r2 = slot < slot_count && sub < 3 ? read(rdram, sub_at(slot, sub) + 0x1A, 2) : 0;
}
// 80099814(type, ...): the screen wipe. The movies use 6 (bars in, 800992C4) and 7 (out,
// 800993FC): a row's bar starts at one edge with a speed of 20-69 pixels a tick.
void srw64_movie_resident_80099814(uint8_t*, recomp_context* ctx) {
    Player* p = current;
    if (!p) return;
    auto& w = p->wipe;
    w.type = int(int32_t(ctx->r4));
    w.on = true;
    if (w.type != 6 && w.type != 7) { warn("wipe type " + std::to_string(w.type) + " is not drawn"); return; }
    for (unsigned row = 0; row < 240; ++row) {
        if (!bar_row(row)) { w.a[row] = w.b[row] = w.speed[row] = 0; continue; }
        p->random = p->random * 1103515245u + 12345u;
        const float speed = float(20 + (p->random >> 16) % 50);
        const bool odd = row & 1;
        w.speed[row] = odd ? -speed : speed;
        if (w.type == 6) w.a[row] = w.b[row] = odd ? 320.0f : 0.0f;
        else { w.a[row] = 0; w.b[row] = 320; }
    }
}
// 800998F4(): whether the wipe has finished.
void srw64_movie_resident_800998F4(uint8_t*, recomp_context* ctx) {
    ctx->r2 = current && current->wipe.on && wipe_done(*current) ? 1 : 0;
}
// 800997D0(): the wipe ends.
void srw64_movie_resident_800997D0(uint8_t*, recomp_context*) {
    if (current) current->wipe.on = false;
}
// 80099C88(slot, sub, palette, kind, ...): a palette effect on a sprite (ゴッドマーズ's
// light). Not drawn: the frames keep their palette.
void srw64_movie_resident_80099C88(uint8_t*, recomp_context* ctx) {
    warn("palette effect " + std::to_string(uint32_t(ctx->r7) & 0xFF) + " on slot " + std::to_string(uint32_t(ctx->r4)) + " is not drawn");
}
// 8007E810(song) / 8007E8A8(sound): played on the game thread.
void srw64_movie_resident_8007E810(uint8_t*, recomp_context* ctx) {
    if (current) current->songs.push_back(int(int32_t(ctx->r4)));
}
void srw64_movie_resident_8007E8A8(uint8_t*, recomp_context* ctx) {
    if (current) current->sounds.push_back(int(int32_t(ctx->r4)));
}
// A jump the generator could not see through to a function it copied.
void srw64_movie_fault(uint8_t*, recomp_context*) {
    if (current) current->failed = true;
    warn("the player jumped outside its copy");
}
}

const Info& info(unsigned movie) { return infos[std::min(movie, count - 1)]; }

unsigned scenes(unsigned movie) { return movie < count ? movie_runs[movie].second : 0; }

unsigned length(unsigned movie) {
    if (movie >= count) return 0;
    {
        std::lock_guard lock(mutex);
        if (const auto found = lengths.find(movie); found != lengths.end()) return found->second;
    }
    auto p = std::make_unique<Player>();
    unsigned ticks = 0;
    if (load(*p, movie)) {
        while (!p->done && !p->failed && p->tick < 30 * 120) tick(*p);
        if (p->done) ticks = p->tick;
    }
    std::lock_guard lock(mutex);
    lengths[movie] = ticks;
    return ticks;
}

Picture preview(unsigned movie, unsigned at) {
    Picture out;
    if (movie >= count) return out;
    auto p = std::make_unique<Player>();
    if (load(*p, movie))
        while (p->tick < at && !p->done && !p->failed) tick(*p);
    draw(*p, out);
    out.movie = int(movie);
    out.tick = p->tick;
    out.failed = p->failed;
    return out;
}

std::optional<Image> frame_image(uint16_t scene_id, uint16_t atlas, uint16_t palette, uint8_t frame) {
    const auto s = scene(scene_id);
    if (!s || frame >= s->frames.size()) return std::nullopt;
    try {
        const auto atlas_bytes = rom_import::resource(rom(), resource_table, atlas).bytes;
        const auto palette_bytes = rom_import::resource(rom(), resource_table, palette).bytes;
        const auto pixels = rom_import::battle_atlas(atlas_bytes, palette_bytes);
        const auto& b = s->bounds[frame];
        Image image;
        image.width = std::max(1, b[2] - b[0]);
        image.height = std::max(1, b[3] - b[1]);
        image.origin[0] = b[0]; image.origin[1] = b[1];
        image.rgba.assign(size_t(image.width) * image.height * 4, 0);
        for (const auto& a : s->frames[frame])
            for (unsigned y = 0; y < a.h; ++y)
                for (unsigned x = 0; x < a.w; ++x) {
                    const unsigned sx = a.s + ((a.flags & 0x10) ? a.w - 1 - x : x), sy = a.t + y;
                    if (sx >= pixels.width || sy >= pixels.height) continue;
                    const size_t src = (size_t(sy) * pixels.width + sx) * 4;
                    const size_t dst = (size_t(a.y - b[1] + int(y)) * image.width + size_t(a.x - b[0] + int(x))) * 4;
                    if (pixels.rgba[src + 3]) std::copy_n(pixels.rgba.begin() + long(src), 4, image.rgba.begin() + long(dst));
                }
        return image;
    } catch (const std::exception& error) {
        warn("frame " + std::to_string(scene_id) + "/" + std::to_string(frame) + ": " + error.what());
        return std::nullopt;
    }
}

void play(unsigned movie) {
    std::lock_guard lock(mutex);
    request = Request{int(std::min(movie, count - 1))};
    paused = false;
}
void pause(bool on) { std::lock_guard lock(mutex); paused = on; }
void step(int ticks) { std::lock_guard lock(mutex); request.step += ticks; }
void set_loop(bool on) { std::lock_guard lock(mutex); looping = on; shown.looping = on; ++shown.serial; }
void stop() { std::lock_guard lock(mutex); request = Request{}; request.stop = true; }
Picture picture() { std::lock_guard lock(mutex); return shown; }
Played played() { std::lock_guard lock(mutex); return heard; }

void game_frame(uint8_t* ram, recomp_context* ctx) {
    Request wanted;
    bool hold, loop;
    {
        std::lock_guard lock(mutex);
        wanted = request;
        request = Request{};
        hold = paused;
        loop = looping;
    }
    if (wanted.movie < 0 && !wanted.stop && !wanted.step && !playing) return;
    const auto call = [&](void (*function)(uint8_t*, recomp_context*), uint32_t a0) {
        auto c = *ctx; c.r29 = int32_t(uint32_t(ctx->r29) - 0x200); c.r4 = int32_t(a0); function(ram, &c);
    };
    constexpr uint32_t song_playing = 0x800FFA6C;
    const auto music = [&](int song) {
        if (title_song == -2) title_song = int16_t(read(ram, song_playing, 2));
        call(resident_func_8007E810, uint32_t(song));
        music_on = true;
        std::lock_guard lock(mutex);
        heard.songs.push_back(song);
    };
    const auto give_back = [&] {
        if (title_song != -2) call(resident_func_8007E810, uint32_t(title_song));
        title_song = -2;
        music_on = false;
    };
    // The movie's song stops; the title's comes back with give_back().
    const auto silence = [&] {
        if (music_on) call(resident_func_8007E810, uint32_t(-1));
        music_on = false;
    };
    const auto publish = [&](bool finished) {
        std::lock_guard lock(mutex);
        Picture next;
        next.serial = shown.serial + 1;
        if (playing) {
            draw(*playing, next);
            next.movie = int(playing->movie);
            next.tick = playing->tick;
            next.failed = playing->failed;
            const auto found = lengths.find(playing->movie);
            next.length = found != lengths.end() ? found->second : 0;
        }
        next.playing = playing && !finished;
        next.paused = paused;
        next.finished = finished;
        next.looping = looping;
        heard.tick = next.tick;
        heard.playing = next.playing;
        shown = std::move(next);
    };
    const auto start = [&](unsigned movie) {
        { std::lock_guard lock(mutex); heard = Played{int(movie)}; }
        playing = std::make_unique<Player>();
        if (!load(*playing, movie)) { playing->failed = true; publish(true); return; }
        for (const int song : playing->songs) music(song);
        playing->songs.clear();
        started = std::chrono::steady_clock::now();
        ticked = 0;
        publish(false);
    };
    if (wanted.stop) {
        playing.reset();
        give_back();
        publish(false);
        return;
    }
    if (wanted.movie >= 0) {
        length(unsigned(wanted.movie));
        start(unsigned(wanted.movie));
        return;
    }
    if (!playing) return;
    if (wanted.step) {
        // One tick on, or the run again from the start to the tick before (silently).
        const unsigned target = unsigned(std::max(0, int(playing->tick) + wanted.step));
        if (target < playing->tick) {
            const unsigned movie = playing->movie;
            playing = std::make_unique<Player>();
            load(*playing, movie);
        }
        while (playing->tick < target && !playing->done && !playing->failed) tick(*playing);
        playing->songs.clear();
        playing->sounds.clear();
        silence();
        publish(playing->done);
        return;
    }
    if (hold) {
        // Paused: the music stops (it could not go on from where it was).
        silence();
        started = std::chrono::steady_clock::now();
        ticked = 0;
        bool shown_paused;
        { std::lock_guard lock(mutex); shown_paused = shown.paused; }
        if (!shown_paused) publish(playing->done || playing->failed);   // the page's button follows
        return;
    }
    if (playing->done || playing->failed) {
        if (loop && playing->done) { start(playing->movie); return; }
        return;
    }
    // 30 ticks a second whatever the game's own frame rate, at most two at once.
    const auto due = unsigned(std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count() / tick_seconds);
    for (unsigned n = 0; ticked < due && n < 2; ++n, ++ticked) {
        tick(*playing);
        for (const int song : playing->songs) music(song);
        for (const int sound : playing->sounds) {
            call(resident_func_8007E8A8, uint32_t(sound));
            std::lock_guard lock(mutex);
            heard.sounds.push_back({playing->tick, sound});
        }
        playing->songs.clear();
        playing->sounds.clear();
        if (playing->done || playing->failed) break;
    }
    if (ticked < due) { started = std::chrono::steady_clock::now(); ticked = 0; }   // a slow frame: no catching up
    const bool finished = playing->done || playing->failed;
    if (finished && !loop) give_back();
    publish(finished);
}
}
