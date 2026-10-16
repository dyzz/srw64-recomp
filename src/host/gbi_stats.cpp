#include "gbi_stats.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "json/json.hpp"

namespace srw64::gbi_stats {
namespace {
// F3DEX2 (the game's microcode; graphics.cpp loads it as such) and the RDP commands.
constexpr uint8_t G_NOOP = 0x00, G_VTX = 0x01, G_MODIFYVTX = 0x02, G_CULLDL = 0x03, G_BRANCH_Z = 0x04,
    G_TRI1 = 0x05, G_TRI2 = 0x06, G_QUAD = 0x07, G_LINE3D = 0x08,
    G_EXTENDED = 0x64,  // RT64's extended opcode (rt64_extended_gbi.h), enabled for every list
    G_SPECIAL_1 = 0xD5, G_DMA_IO = 0xD6, G_TEXTURE = 0xD7, G_POPMTX = 0xD8, G_GEOMETRYMODE = 0xD9,
    G_MTX = 0xDA, G_MOVEWORD = 0xDB, G_MOVEMEM = 0xDC, G_LOAD_UCODE = 0xDD, G_DL = 0xDE, G_ENDDL = 0xDF,
    G_SPNOOP = 0xE0, G_RDPHALF_1 = 0xE1, G_SETOTHERMODE_L = 0xE2, G_SETOTHERMODE_H = 0xE3,
    G_TEXRECT = 0xE4, G_TEXRECTFLIP = 0xE5, G_RDPLOADSYNC = 0xE6, G_RDPPIPESYNC = 0xE7, G_RDPTILESYNC = 0xE8,
    G_RDPFULLSYNC = 0xE9, G_SETKEYGB = 0xEA, G_SETKEYR = 0xEB, G_SETCONVERT = 0xEC, G_SETSCISSOR = 0xED,
    G_SETPRIMDEPTH = 0xEE, G_RDPSETOTHERMODE = 0xEF, G_LOADTLUT = 0xF0, G_RDPHALF_2 = 0xF1,
    G_SETTILESIZE = 0xF2, G_LOADBLOCK = 0xF3, G_LOADTILE = 0xF4, G_SETTILE = 0xF5, G_FILLRECT = 0xF6,
    G_SETFILLCOLOR = 0xF7, G_SETFOGCOLOR = 0xF8, G_SETBLENDCOLOR = 0xF9, G_SETPRIMCOLOR = 0xFA,
    G_SETENVCOLOR = 0xFB, G_SETCOMBINE = 0xFC, G_SETTIMG = 0xFD, G_SETZIMG = 0xFE, G_SETCIMG = 0xFF;

const char* opcode_name(uint8_t op) {
    switch (op) {
    case G_NOOP: return "G_NOOP"; case G_VTX: return "G_VTX"; case G_MODIFYVTX: return "G_MODIFYVTX";
    case G_CULLDL: return "G_CULLDL"; case G_BRANCH_Z: return "G_BRANCH_Z"; case G_TRI1: return "G_TRI1";
    case G_TRI2: return "G_TRI2"; case G_QUAD: return "G_QUAD"; case G_LINE3D: return "G_LINE3D";
    case G_EXTENDED: return "G_EX (RT64)"; case G_SPECIAL_1: return "G_SPECIAL_1"; case G_DMA_IO: return "G_DMA_IO";
    case G_TEXTURE: return "G_TEXTURE"; case G_POPMTX: return "G_POPMTX"; case G_GEOMETRYMODE: return "G_GEOMETRYMODE";
    case G_MTX: return "G_MTX"; case G_MOVEWORD: return "G_MOVEWORD"; case G_MOVEMEM: return "G_MOVEMEM";
    case G_LOAD_UCODE: return "G_LOAD_UCODE"; case G_DL: return "G_DL"; case G_ENDDL: return "G_ENDDL";
    case G_SPNOOP: return "G_SPNOOP"; case G_RDPHALF_1: return "G_RDPHALF_1"; case G_SETOTHERMODE_L: return "G_SETOTHERMODE_L";
    case G_SETOTHERMODE_H: return "G_SETOTHERMODE_H"; case G_TEXRECT: return "G_TEXRECT"; case G_TEXRECTFLIP: return "G_TEXRECTFLIP";
    case G_RDPLOADSYNC: return "G_RDPLOADSYNC"; case G_RDPPIPESYNC: return "G_RDPPIPESYNC"; case G_RDPTILESYNC: return "G_RDPTILESYNC";
    case G_RDPFULLSYNC: return "G_RDPFULLSYNC"; case G_SETKEYGB: return "G_SETKEYGB"; case G_SETKEYR: return "G_SETKEYR";
    case G_SETCONVERT: return "G_SETCONVERT"; case G_SETSCISSOR: return "G_SETSCISSOR"; case G_SETPRIMDEPTH: return "G_SETPRIMDEPTH";
    case G_RDPSETOTHERMODE: return "G_RDPSETOTHERMODE"; case G_LOADTLUT: return "G_LOADTLUT"; case G_RDPHALF_2: return "G_RDPHALF_2";
    case G_SETTILESIZE: return "G_SETTILESIZE"; case G_LOADBLOCK: return "G_LOADBLOCK"; case G_LOADTILE: return "G_LOADTILE";
    case G_SETTILE: return "G_SETTILE"; case G_FILLRECT: return "G_FILLRECT"; case G_SETFILLCOLOR: return "G_SETFILLCOLOR";
    case G_SETFOGCOLOR: return "G_SETFOGCOLOR"; case G_SETBLENDCOLOR: return "G_SETBLENDCOLOR"; case G_SETPRIMCOLOR: return "G_SETPRIMCOLOR";
    case G_SETENVCOLOR: return "G_SETENVCOLOR"; case G_SETCOMBINE: return "G_SETCOMBINE"; case G_SETTIMG: return "G_SETTIMG";
    case G_SETZIMG: return "G_SETZIMG"; case G_SETCIMG: return "G_SETCIMG";
    default: return nullptr;
    }
}

// Words each RT64 extended command occupies (rt64_gbi_extended.cpp); 0 for the ones whose
// length is not known here (none of them is emitted by this host).
int extended_words(uint32_t op) {
    switch (op) {
    case 0x02: return 6;               // texrect
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: return 4;  // fillrect, viewport, scissor, rect align, viewport align
    case 0x08: return 6;               // scissor align
    case 0x0C: case 0x13: case 0x14: return 4;  // matrix group, edit group by address, vertex
    case 0x2D: case 0x2E: case 0x30: return 0;  // float matrices: unknown here
    default: return op < 0x34 ? 2 : 0;
    }
}

struct Stats {
    std::mutex mutex;
    std::string path;
    uint64_t lists = 0, commands = 0, truncated = 0, unknown_opcodes = 0, unknown_extended = 0, deepest_call = 0;
    std::map<uint8_t, uint64_t> opcodes;            // by opcode
    std::map<std::string, uint64_t> unknown_sites;   // an unknown opcode's words and the command before it
    unsigned unknown_dumps = 0;
    std::map<uint32_t, uint64_t> extended;           // by extended sub-op
    std::map<std::string, uint64_t> combine;         // "w0 w1" of G_SETCOMBINE
    std::map<std::string, uint64_t> draw_states;     // "othermode_h othermode_l combine_w0 combine_w1 geometry" at each draw
    std::map<std::string, uint64_t> othermode;       // "h l" at each draw
    std::map<std::string, uint64_t> image_formats;   // G_SETTIMG "fmt siz"
    std::map<std::string, uint64_t> tile_formats;    // G_SETTILE "fmt siz"
    std::map<std::string, uint64_t> color_images;    // G_SETCIMG "fmt siz width"
    std::map<std::string, uint64_t> tile_sizes;      // G_SETTILESIZE "w h" in texels (lr - ul + 1)
    std::map<uint32_t, uint64_t> moveword_index, movemem_index, mtx_params, texture_tiles, tlut_sizes;
    std::map<uint32_t, uint64_t> vtx_counts;         // vertices per G_VTX
    uint32_t geometry_set = 0, geometry_cleared = 0;
    uint64_t draws_tri = 0, draws_texrect = 0, draws_fillrect = 0, draws_quad = 0, draws_line = 0, vertices = 0, ucode_loads = 0, branch_z = 0;
    std::vector<uint32_t> draws_per_list, commands_per_list, tris_per_list;
    // RDP state carried across lists, as on the hardware.
    uint32_t othermode_h = 0, othermode_l = 0, combine_w0 = 0, combine_w1 = 0, geometry = 0;
    uint32_t segments[16] = {};
};

Stats& stats() {
    static Stats instance;
    return instance;
}

std::string hex(uint32_t value) {
    char text[12];
    std::snprintf(text, sizeof text, "%08X", value);
    return text;
}

uint32_t word(const uint8_t* memory, uint32_t physical) {
    uint32_t value;
    std::memcpy(&value, memory + physical, 4);
    return value;
}

void walk(Stats& s, const uint8_t* list, const uint8_t* rdram, uint32_t start, uint32_t size) {
    constexpr uint32_t ram = 8 * 1024 * 1024;
    uint32_t cursor = start & 0x1FFFFFFF;
    const uint32_t top = cursor, top_end = top + size;
    // The top-level list from its copy, everything it calls from RDRAM (RT64's rule).
    auto memory_at = [&](uint32_t physical) { return physical >= top && physical < top_end ? list : rdram; };
    uint32_t stack[32];
    size_t depth = 0;
    uint64_t commands = 0;
    uint32_t draws = 0, tris = 0, previous = 0;
    auto resolve = [&](uint32_t address) { return (s.segments[(address >> 24) & 0xF] + (address & 0x00FFFFFF)) & 0x1FFFFFFF; };
    auto draw = [&](uint32_t count) {
        draws += count;
        s.draw_states[hex(s.othermode_h) + " " + hex(s.othermode_l) + " " + hex(s.combine_w0) + " " + hex(s.combine_w1) + " " + hex(s.geometry)]++;
        s.othermode[hex(s.othermode_h) + " " + hex(s.othermode_l)]++;
    };
    while (cursor + 8 <= ram) {
        if (++commands > 400000) { ++s.truncated; break; }
        const uint8_t* memory = memory_at(cursor);
        const uint32_t w0 = word(memory, cursor), w1 = word(memory, cursor + 4);
        const uint8_t op = uint8_t(w0 >> 24);
        s.opcodes[op]++;
        cursor += 8;
        switch (op) {
        case G_DL: {
            const uint32_t target = resolve(w1);
            if (((w0 >> 16) & 0xFF) == 0) {   // push
                if (depth < std::size(stack)) stack[depth++] = cursor;
                else { ++s.truncated; cursor = ram; }
                s.deepest_call = std::max<uint64_t>(s.deepest_call, depth);
            }
            cursor = target;
            break;
        }
        case G_ENDDL:
            if (depth == 0) cursor = ram;
            else cursor = stack[--depth];
            break;
        case G_BRANCH_Z: ++s.branch_z; break;   // conditional on a vertex's depth: the branch is not followed here
        case G_LOAD_UCODE: ++s.ucode_loads; break;
        case G_MOVEWORD: {
            const uint32_t index = (w0 >> 16) & 0xFF;
            s.moveword_index[index]++;
            if (index == 6) s.segments[((w0 & 0xFFFF) >> 2) & 0xF] = w1 & 0x00FFFFFF;   // G_MW_SEGMENT
            break;
        }
        case G_MOVEMEM: s.movemem_index[w0 & 0xFF]++; break;
        case G_MTX: s.mtx_params[(w0 & 0xFF) ^ 1]++; break;   // F3DEX2 inverts the push bit
        case G_GEOMETRYMODE: {
            const uint32_t clear = ~w0 & 0x00FFFFFF;
            s.geometry_cleared |= clear;
            s.geometry_set |= w1;
            s.geometry = (s.geometry & ~clear) | w1;
            break;
        }
        case G_VTX: { const uint32_t count = (w0 >> 12) & 0xFF; s.vtx_counts[count]++; s.vertices += count; break; }
        case G_TRI1: ++s.draws_tri; ++tris; draw(1); break;
        case G_TRI2: s.draws_tri += 2; tris += 2; draw(2); break;
        case G_QUAD: ++s.draws_quad; tris += 2; draw(2); break;
        case G_LINE3D: ++s.draws_line; draw(1); break;
        case G_TEXRECT: case G_TEXRECTFLIP: ++s.draws_texrect; draw(1); break;
        case G_FILLRECT: ++s.draws_fillrect; draw(1); break;
        case G_TEXTURE: s.texture_tiles[(w0 >> 8) & 0x7]++; break;
        case G_SETOTHERMODE_H: case G_SETOTHERMODE_L: {
            const uint32_t length = (w0 & 0xFF) + 1, shift = 32 - ((w0 >> 8) & 0xFF) - length;
            const uint32_t mask = (length >= 32 ? 0xFFFFFFFFu : ((1u << length) - 1)) << shift;
            uint32_t& mode = op == G_SETOTHERMODE_H ? s.othermode_h : s.othermode_l;
            mode = (mode & ~mask) | (w1 & mask);
            break;
        }
        case G_RDPSETOTHERMODE: s.othermode_h = w0 & 0x00FFFFFF; s.othermode_l = w1; break;
        case G_SETCOMBINE:
            s.combine_w0 = w0 & 0x00FFFFFF; s.combine_w1 = w1;
            s.combine[hex(s.combine_w0) + " " + hex(s.combine_w1)]++;
            break;
        case G_SETTIMG: s.image_formats[std::to_string((w0 >> 21) & 7) + " " + std::to_string((w0 >> 19) & 3)]++; break;
        case G_SETTILE: s.tile_formats[std::to_string((w0 >> 21) & 7) + " " + std::to_string((w0 >> 19) & 3)]++; break;
        case G_SETCIMG: s.color_images[std::to_string((w0 >> 21) & 7) + " " + std::to_string((w0 >> 19) & 3) + " " + std::to_string((w0 & 0xFFF) + 1)]++; break;
        case G_SETTILESIZE: {
            const uint32_t uls = (w0 >> 12) & 0xFFF, ult = w0 & 0xFFF, lrs = (w1 >> 12) & 0xFFF, lrt = w1 & 0xFFF;
            s.tile_sizes[std::to_string((lrs - uls) / 4 + 1) + " " + std::to_string((lrt - ult) / 4 + 1)]++;
            break;
        }
        case G_LOADTLUT: s.tlut_sizes[(((w1 >> 14) & 0x3FF) + 1)]++; break;
        case G_EXTENDED: {
            const uint32_t sub = w0 & 0x00FFFFFF;
            s.extended[sub]++;
            const int words = extended_words(sub);
            if (words == 0) { ++s.unknown_extended; cursor = ram; break; }
            cursor += uint32_t(words - 2) * 4;
            break;
        }
        default:
            if (!opcode_name(op)) {
                ++s.unknown_opcodes;
                s.unknown_sites[hex(uint32_t(op) << 24) + " w0=" + hex(w0) + " w1=" + hex(w1) + " previous=" + hex(previous)]++;
                // The first few: the list's words from its start, for a look offline.
                if (s.unknown_dumps < 4) {
                    const std::filesystem::path dump = s.path + ".unknown-" + std::to_string(s.unknown_dumps++) + ".bin";
                    std::ofstream file(dump, std::ios::binary);
                    const uint32_t from = start & 0x1FFFFFFF, length = std::min<uint32_t>(ram - from, 0x10000);
                    file.write(reinterpret_cast<const char*>(rdram + from), length);
                    std::fprintf(stderr, "SRW64_GBI_STATS unknown opcode %02X at %08X in list %08X, dumped to %s\n", op, 0x80000000u + cursor - 8, 0x80000000u + from, dump.string().c_str());
                }
                cursor = ram;
            }
            break;
        }
        previous = w0;
    }
    s.commands += commands;
    s.draws_per_list.push_back(draws);
    s.tris_per_list.push_back(tris);
    s.commands_per_list.push_back(uint32_t(commands));
}

nlohmann::json histogram(std::vector<uint32_t> values) {
    if (values.empty()) return {{"count", 0}};
    std::sort(values.begin(), values.end());
    uint64_t sum = 0;
    for (auto v : values) sum += v;
    auto at = [&](double q) { return values[std::min(values.size() - 1, size_t(q * values.size()))]; };
    return {{"count", values.size()}, {"min", values.front()}, {"max", values.back()},
            {"mean", double(sum) / values.size()}, {"p50", at(.5)}, {"p90", at(.9)}, {"p99", at(.99)}};
}

template <class Key> nlohmann::json counts(const std::map<Key, uint64_t>& map, const char* (*name)(uint8_t) = nullptr) {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, count] : map) {
        std::string label;
        if constexpr (std::is_same_v<Key, std::string>) label = key;
        else {
            const char* known = name ? name(uint8_t(key)) : nullptr;
            label = known ? std::string(known) : hex(uint32_t(key));
        }
        out[label] = count;
    }
    return out;
}

void write(Stats& s) {
    nlohmann::json out = {
        {"schema", "srw64.gbi-stats.v1"},
        {"lists", s.lists}, {"commands", s.commands}, {"truncated_lists", s.truncated},
        {"unknown_opcodes", s.unknown_opcodes}, {"unknown_extended", s.unknown_extended},
        {"deepest_call", s.deepest_call}, {"ucode_loads", s.ucode_loads}, {"branch_z", s.branch_z},
        {"draws", {{"triangles", s.draws_tri}, {"quads", s.draws_quad}, {"lines", s.draws_line},
                   {"texture_rectangles", s.draws_texrect}, {"fill_rectangles", s.draws_fillrect}, {"vertices", s.vertices}}},
        {"per_list", {{"draws", histogram(s.draws_per_list)}, {"triangles", histogram(s.tris_per_list)},
                      {"commands", histogram(s.commands_per_list)}}},
        {"opcodes", counts(s.opcodes, opcode_name)}, {"unknown_sites", counts(s.unknown_sites)}, {"extended", counts(s.extended)},
        {"combine", counts(s.combine)}, {"othermode", counts(s.othermode)}, {"draw_states", counts(s.draw_states)},
        {"image_formats", counts(s.image_formats)}, {"tile_formats", counts(s.tile_formats)},
        {"color_images", counts(s.color_images)}, {"tile_sizes", counts(s.tile_sizes)},
        {"tlut_sizes", counts(s.tlut_sizes)}, {"moveword", counts(s.moveword_index)}, {"movemem", counts(s.movemem_index)},
        {"mtx_params", counts(s.mtx_params)}, {"texture_tiles", counts(s.texture_tiles)}, {"vtx_counts", counts(s.vtx_counts)},
        {"geometry_mode", {{"set", hex(s.geometry_set)}, {"cleared", hex(s.geometry_cleared)}}},
    };
    const std::filesystem::path path(s.path), temporary = path.string() + ".tmp";
    std::ofstream(temporary) << out.dump(1) << '\n';
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}
}  // namespace

bool enabled() {
    static const bool value = [] {
        const char* debug = std::getenv("SRW64_DEBUG");
        const char* path = std::getenv("SRW64_GBI_STATS");
        if (!debug || std::string(debug) != "1" || !path || !*path) return false;
        stats().path = path;
        return true;
    }();
    return value;
}

void record(const uint8_t* list, const uint8_t* rdram, uint32_t start, uint32_t size) {
    if (!enabled()) return;
    Stats& s = stats();
    std::lock_guard lock(s.mutex);
    ++s.lists;
    walk(s, list, rdram, start, size);
    if (s.lists % 120 == 0) write(s);
}

void flush() {
    if (!enabled()) return;
    Stats& s = stats();
    std::lock_guard lock(s.mutex);
    write(s);
}

namespace {
struct Dumps {
    std::mutex mutex;
    std::filesystem::path folder;
    uint32_t every = 30, max = 40, run = 1, seen = 0, written = 0;
    std::map<uint64_t, std::string> shots;   // workload -> PNG path
};
Dumps* dumps() {
    static Dumps* value = []() -> Dumps* {
        const char* debug = std::getenv("SRW64_DEBUG");
        const char* folder = std::getenv("SRW64_GL_DUMP");
        if (!debug || std::string(debug) != "1" || !folder || !*folder) return nullptr;
        auto* d = new Dumps;
        d->folder = folder;
        if (const char* every = std::getenv("SRW64_GL_DUMP_EVERY")) d->every = std::max(1, std::atoi(every));
        if (const char* max = std::getenv("SRW64_GL_DUMP_MAX")) d->max = std::max(0, std::atoi(max));
        if (const char* run = std::getenv("SRW64_GL_DUMP_RUN")) d->run = std::max(1, std::atoi(run));
        std::error_code error;
        std::filesystem::create_directories(d->folder, error);
        return d;
    }();
    return value;
}
}  // namespace

void dump(const uint8_t* list, const uint8_t* rdram, uint32_t start, uint32_t size, uint64_t workload) {
    Dumps* d = dumps();
    if (!d) return;
    std::lock_guard lock(d->mutex);
    if (d->seen++ % d->every >= d->run || d->written >= d->max) return;
    constexpr uint32_t ram = 8 * 1024 * 1024;
    char name[16];
    std::snprintf(name, sizeof name, "%04u", d->written++);
    std::vector<uint8_t> image(rdram, rdram + ram);
    const uint32_t top = start & 0x1FFFFFFF;
    if (list != rdram && top < ram) std::memcpy(image.data() + top, list + top, std::min(size, ram - top));
    std::ofstream(d->folder / (std::string(name) + ".rdram"), std::ios::binary).write(reinterpret_cast<const char*>(image.data()), image.size());
    const nlohmann::json meta = {{"schema", "srw64.gl-dump.v1"}, {"start", start}, {"size", size}, {"workload", workload}, {"list", d->seen - 1}};
    std::ofstream(d->folder / (std::string(name) + ".json")) << meta.dump(1) << '\n';
    d->shots[workload] = (d->folder / (std::string(name) + ".rt64.png")).string();
}

std::string take_shot(uint64_t workload) {
    Dumps* d = dumps();
    if (!d) return {};
    std::lock_guard lock(d->mutex);
    const auto found = d->shots.find(workload);
    if (found == d->shots.end()) return {};
    auto path = std::move(found->second);
    d->shots.erase(found);
    return path;
}
}  // namespace srw64::gbi_stats
