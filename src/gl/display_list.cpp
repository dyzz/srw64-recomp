#include "display_list.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace srw64::n64gl {
namespace {
// F3DEX2 and RDP opcodes (the game's microcode; graphics.cpp loads it as such).
enum : uint8_t {
    G_VTX = 0x01, G_MODIFYVTX = 0x02, G_CULLDL = 0x03, G_BRANCH_Z = 0x04, G_TRI1 = 0x05, G_TRI2 = 0x06,
    G_QUAD = 0x07, G_EXTENDED = 0x64, G_TEXTURE = 0xD7, G_POPMTX = 0xD8, G_GEOMETRYMODE = 0xD9,
    G_MTX = 0xDA, G_MOVEWORD = 0xDB, G_MOVEMEM = 0xDC, G_DL = 0xDE, G_ENDDL = 0xDF,
    G_RDPHALF_1 = 0xE1, G_SETOTHERMODE_L = 0xE2, G_SETOTHERMODE_H = 0xE3, G_TEXRECT = 0xE4,
    G_TEXRECTFLIP = 0xE5, G_SETSCISSOR = 0xED, G_SETPRIMDEPTH = 0xEE, G_RDPSETOTHERMODE = 0xEF,
    G_LOADTLUT = 0xF0, G_RDPHALF_2 = 0xF1, G_SETTILESIZE = 0xF2, G_LOADBLOCK = 0xF3, G_LOADTILE = 0xF4,
    G_SETTILE = 0xF5, G_FILLRECT = 0xF6, G_SETFILLCOLOR = 0xF7, G_SETFOGCOLOR = 0xF8,
    G_SETBLENDCOLOR = 0xF9, G_SETPRIMCOLOR = 0xFA, G_SETENVCOLOR = 0xFB, G_SETCOMBINE = 0xFC,
    G_SETTIMG = 0xFD, G_SETZIMG = 0xFE, G_SETCIMG = 0xFF,
};
// Geometry mode (F3DEX2).
enum : uint32_t {
    G_ZBUFFER = 0x1, G_SHADE = 0x4, G_CULL_FRONT = 0x200, G_CULL_BACK = 0x400, G_LIGHTING = 0x20000,
    G_SHADING_SMOOTH = 0x200000,
};
// Other mode, low word.
enum : uint32_t {
    Z_CMP = 0x10, Z_UPD = 0x20, CVG_X_ALPHA = 0x1000, FORCE_BL = 0x4000,
};
enum : uint8_t { G_IM_FMT_RGBA = 0, G_IM_FMT_YUV = 1, G_IM_FMT_CI = 2, G_IM_FMT_IA = 3, G_IM_FMT_I = 4 };
enum : uint8_t { G_IM_SIZ_4b = 0, G_IM_SIZ_8b = 1, G_IM_SIZ_16b = 2, G_IM_SIZ_32b = 3 };
constexpr uint32_t kRam = 8 * 1024 * 1024;

// Words each RT64 extended command occupies (rt64_gbi_extended.cpp; gbi_stats.cpp).
int extended_words(uint32_t op) {
    switch (op) {
    case 0x02: return 6;
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: return 4;
    case 0x08: return 6;
    case 0x0C: case 0x13: case 0x14: return 4;
    case 0x2D: case 0x2E: case 0x30: return 0;
    default: return op < 0x34 ? 2 : 0;
    }
}

struct Matrix { float m[4][4]; };

Matrix multiply(const Matrix& a, const Matrix& b) {   // a then b, for row vectors (v * a * b)
    Matrix r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
        r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

Matrix identity() {
    Matrix r{};
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1;
    return r;
}

struct Tile {
    uint8_t fmt = 0, siz = 0, palette = 0, cms = 0, cmt = 0, masks = 0, maskt = 0, shifts = 0, shiftt = 0;
    uint16_t line = 0, tmem = 0;
    uint16_t uls = 0, ult = 0, lrs = 0, lrt = 0;   // 10.2
};

struct Light { uint8_t color[3]; int8_t dir[3]; };

struct RspVertex {
    float x, y, z, w;      // picture clip space
    float s, t;            // texels (10.5 scaled by G_TEXTURE), before the tile
    uint8_t color[4];
};

uint64_t mix(uint64_t h, uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    h *= 0xFF51AFD7ED558CCDull;
    return h ^ (h >> 33);
}

uint64_t hash_bytes(uint64_t h, const uint8_t* data, size_t size) {
    size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        uint64_t v;
        std::memcpy(&v, data + i, 8);
        h = mix(h, v);
    }
    uint64_t tail = 0;
    for (; i < size; ++i) tail = (tail << 8) | data[i];
    return mix(h, tail ^ size);
}

uint32_t expand5551(uint16_t c) {
    const uint32_t r = (c >> 11) & 31, g = (c >> 6) & 31, b = (c >> 1) & 31;
    const uint32_t r8 = (r << 3) | (r >> 2), g8 = (g << 3) | (g >> 2), b8 = (b << 3) | (b >> 2);
    return r8 | (g8 << 8) | (b8 << 16) | ((c & 1) ? 0xFF000000u : 0);
}
}  // namespace

Combiner combiner_of(uint32_t shader) {
    Combiner c;
    c.a = shader & 0xF;
    c.b = (shader >> 4) & 0xF;
    c.c = (shader >> 8) & 0x1F;
    c.d = (shader >> 13) & 0x7;
    c.aa = (shader >> 16) & 0x7;
    c.ab = (shader >> 19) & 0x7;
    c.ac = (shader >> 22) & 0x7;
    c.ad = (shader >> 25) & 0x7;
    return c;
}

struct Interpreter::State {
    std::function<bool(uint64_t)> known;
    const uint8_t* rdram = nullptr;
    Frame* frame = nullptr;
    std::FILE* log = nullptr;
    Classify classify;
    uint32_t tag_w0 = 0, tag_w1 = 0;   // the command before the one running
    uint32_t tagged() const { return classify && tag_w0 >> 24 == 0 ? classify(tag_w0, tag_w1) : 0; }
    void native(uint32_t id, float left, float top, float right, float bottom) {
        Command c;
        c.kind = Command::native;
        c.native_id = id;
        std::memcpy(c.state.scissor, scissor, sizeof scissor);
        c.rect[0] = left; c.rect[1] = top; c.rect[2] = right; c.rect[3] = bottom;
        frame->commands.push_back(c);
        ++frame->natives;
    }
    std::unordered_set<uint64_t> added;   // textures already in this frame's list

    // RSP
    uint32_t segments[16] = {};
    Matrix modelview[18] = {};
    int depth = 0;
    Matrix projection = identity(), mvp = identity();
    bool mvp_dirty = true;
    uint32_t geometry = 0;
    int16_t vscale[4] = {640, 480, 511, 0}, vtrans[4] = {640, 480, 511, 0};
    Light lights[8] = {};
    int lights_count = 1;   // directional lights; the ambient follows them
    uint16_t texture_s = 0xFFFF, texture_t = 0xFFFF;
    uint8_t texture_tile = 0;
    bool texture_on = false;
    RspVertex vertices[64] = {};
    uint32_t half1 = 0, half2 = 0;

    // RDP
    uint32_t othermode_h = 0, othermode_l = 0, combine_w0 = 0, combine_w1 = 0;
    uint32_t prim = 0, env = 0, blend = 0, fill = 0, fog = 0;
    uint32_t timg_addr = 0, timg_width = 1; uint8_t timg_fmt = 0, timg_siz = 0;
    uint32_t cimg = 0, zimg = 0;
    // Colour images seen lately, and TMEM loaded from one of them: by TMEM word, the image
    // and where in it the load began (texels).
    uint32_t pictures[4] = {};
    struct PictureLoad { uint32_t image = 0; int x = 0, y = 0; };
    PictureLoad picture_loads[512];
    uint32_t captured = 0;   // the colour image last captured in this task
    int16_t scissor[4] = {0, 0, kPictureWidth, kPictureHeight};
    // RT64's extended commands that the host writes into the lists (wide_map.cpp,
    // battle_hud.cpp): a scissor stack, and rectangles moved from an origin and by offsets
    // (quarter pixels). An origin is a fraction of the picture's width, 0x400 its right edge.
    int16_t scissor_stack[8][4] = {};
    int scissor_depth = 0;
    uint16_t rect_origin[2] = {0x800, 0x800};
    int32_t rect_offset[4] = {};
    static int32_t from_origin(int32_t x, uint32_t origin) {   // quarter pixels
        return origin < 0x800 ? x + int32_t(origin) * kPictureWidth * 4 / 0x400 : x;
    }
    Tile tiles[8];
    uint8_t tmem[4096] = {};
    uint32_t tmem_version = 1;
    // The last texture each tile resolved to, while TMEM and the tile stay the same.
    struct Memo { uint32_t version = 0; uint64_t signature = 0, key = 0; uint16_t width = 0, height = 0; uint16_t uls = 0, ult = 0; } memo[8];

    uint8_t rd8(uint32_t a) const { return rdram[(a & (kRam - 1)) ^ 3]; }
    uint16_t rd16(uint32_t a) const { uint16_t v; std::memcpy(&v, rdram + ((a & (kRam - 2)) ^ 2), 2); return v; }
    uint32_t rd32(uint32_t a) const { uint32_t v; std::memcpy(&v, rdram + (a & (kRam - 4)), 4); return v; }
    uint32_t segmented(uint32_t address) const {
        return (segments[(address >> 24) & 0xF] + (address & 0x00FFFFFF)) & 0x1FFFFFFF;
    }

    void load_matrix(uint32_t address, Matrix& out) const {
        for (int i = 0; i < 16; ++i) {
            const int32_t whole = int16_t(rd16(address + i * 2));
            const uint32_t fraction = rd16(address + 32 + i * 2);
            out.m[i / 4][i % 4] = float(int32_t(uint32_t(whole) << 16 | fraction)) / 65536.f;
        }
    }

    void matrix(uint32_t w0, uint32_t w1) {
        const uint32_t params = (w0 & 0xFF) ^ 1;   // F3DEX2 inverts the push bit
        Matrix m;
        load_matrix(segmented(w1), m);
        if (params & 4) {   // projection
            projection = (params & 2) ? m : multiply(m, projection);
        } else {
            if ((params & 1) && depth < 17) { modelview[depth + 1] = modelview[depth]; ++depth; }
            modelview[depth] = (params & 2) ? m : multiply(m, modelview[depth]);
        }
        mvp_dirty = true;
    }

    void vertex(uint32_t w0, uint32_t w1) {
        const uint32_t count = (w0 >> 12) & 0xFF, end = (w0 >> 1) & 0x7F;
        if (count == 0 || count > end || end > 64) return;
        const uint32_t first = end - count, address = segmented(w1);
        if (mvp_dirty) { mvp = multiply(modelview[depth], projection); mvp_dirty = false; }
        // Lights in model space (the directions through the modelview's rotation).
        float coefficients[8][3] = {};
        const bool lit = geometry & G_LIGHTING;
        if (lit) {
            const Matrix& mv = modelview[depth];
            for (int l = 0; l < lights_count; ++l) {
                const float d[3] = {lights[l].dir[0] / 127.f, lights[l].dir[1] / 127.f, lights[l].dir[2] / 127.f};
                float c[3];
                for (int i = 0; i < 3; ++i) c[i] = d[0] * mv.m[i][0] + d[1] * mv.m[i][1] + d[2] * mv.m[i][2];
                const float length = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
                for (int i = 0; i < 3; ++i) coefficients[l][i] = length > 0 ? c[i] / length : 0;
            }
        }
        // The viewport, folded into clip space: x_screen = trans + ndc * scale (quarter pixels).
        const float kx = vscale[0] / (4.f * kPictureWidth / 2), tx = vtrans[0] / (4.f * kPictureWidth / 2) - 1;
        const float ky = vscale[1] / (4.f * kPictureHeight / 2), ty = 1 - vtrans[1] / (4.f * kPictureHeight / 2);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t at = address + i * 16;
            const float x = int16_t(rd16(at)), y = int16_t(rd16(at + 2)), z = int16_t(rd16(at + 4));
            const float s = int16_t(rd16(at + 8)), t = int16_t(rd16(at + 10));
            RspVertex& v = vertices[first + i];
            const float cx = x * mvp.m[0][0] + y * mvp.m[1][0] + z * mvp.m[2][0] + mvp.m[3][0];
            const float cy = x * mvp.m[0][1] + y * mvp.m[1][1] + z * mvp.m[2][1] + mvp.m[3][1];
            const float cz = x * mvp.m[0][2] + y * mvp.m[1][2] + z * mvp.m[2][2] + mvp.m[3][2];
            const float cw = x * mvp.m[0][3] + y * mvp.m[1][3] + z * mvp.m[2][3] + mvp.m[3][3];
            v.x = cx * kx + cw * tx;
            v.y = cy * ky + cw * ty;
            v.z = cz;
            v.w = cw;
            v.s = s * texture_s / 65536.f / 32.f;
            v.t = t * texture_t / 65536.f / 32.f;
            const uint8_t b0 = rd8(at + 12), b1 = rd8(at + 13), b2 = rd8(at + 14), b3 = rd8(at + 15);
            if (lit) {
                const float n[3] = {float(int8_t(b0)), float(int8_t(b1)), float(int8_t(b2))};
                float color[3] = {float(lights[lights_count].color[0]), float(lights[lights_count].color[1]), float(lights[lights_count].color[2])};
                for (int l = 0; l < lights_count; ++l) {
                    const float intensity = (n[0] * coefficients[l][0] + n[1] * coefficients[l][1] + n[2] * coefficients[l][2]) / 127.f;
                    if (intensity > 0) for (int c = 0; c < 3; ++c) color[c] += intensity * lights[l].color[c];
                }
                for (int c = 0; c < 3; ++c) v.color[c] = uint8_t(std::min(color[c], 255.f));
            } else {
                v.color[0] = b0; v.color[1] = b1; v.color[2] = b2;
            }
            v.color[3] = b3;
        }
    }

    void moveword(uint32_t w0, uint32_t w1) {
        const uint32_t index = (w0 >> 16) & 0xFF, offset = w0 & 0xFFFF;
        switch (index) {
        case 0x02: lights_count = std::clamp(int(w1 / 24), 0, 7); break;    // G_MW_NUMLIGHT
        case 0x06: segments[(offset >> 2) & 0xF] = w1 & 0x00FFFFFF; break;  // G_MW_SEGMENT
        case 0x0A: {                                                          // G_MW_LIGHTCOL
            const uint32_t light = offset / 24;
            if (light < 8 && (offset % 24) == 0) {
                lights[light].color[0] = uint8_t(w1 >> 24); lights[light].color[1] = uint8_t(w1 >> 16); lights[light].color[2] = uint8_t(w1 >> 8);
            }
            break;
        }
        default: break;   // G_MW_CLIP, G_MW_PERSPNORM and the rest change nothing drawn here
        }
    }

    void movemem(uint32_t w0, uint32_t w1) {
        const uint32_t index = w0 & 0xFF, offset = ((w0 >> 8) & 0xFF) * 8, address = segmented(w1);
        if (index == 8) {   // G_MV_VIEWPORT
            for (int i = 0; i < 4; ++i) { vscale[i] = int16_t(rd16(address + i * 2)); vtrans[i] = int16_t(rd16(address + 8 + i * 2)); }
        } else if (index == 10) {   // G_MV_LIGHT: two look-at slots, then the lights
            const int light = int(offset / 24) - 2;
            if (light >= 0 && light < 8) {
                for (int c = 0; c < 3; ++c) lights[light].color[c] = rd8(address + c);
                for (int c = 0; c < 3; ++c) lights[light].dir[c] = int8_t(rd8(address + 8 + c));
            }
        }
    }

    // TMEM loads, kept as the RDP keeps them: big-endian, odd lines with their 32-bit words swapped.
    static uint32_t bytes_of(uint32_t texels, uint8_t siz) { return siz == G_IM_SIZ_4b ? texels / 2 : texels << (siz - 1); }

    // The colour image a texture image address lies in, 0 if none.
    uint32_t picture_at(uint32_t address) const {
        constexpr uint32_t bytes = kPictureWidth * kPictureHeight * 2;
        for (uint32_t image : pictures) if (image && address >= image && address < image + bytes) return image;
        return 0;
    }

    void load_tile(uint32_t w0, uint32_t w1) {
        const Tile& tile = tiles[(w1 >> 24) & 7];
        const uint32_t uls = ((w0 >> 12) & 0xFFF) >> 2, ult = (w0 & 0xFFF) >> 2;
        const uint32_t lrs = ((w1 >> 12) & 0xFFF) >> 2, lrt = (w1 & 0xFFF) >> 2;
        if (lrs < uls || lrt < ult) return;
        PictureLoad& load = picture_loads[tile.tmem & 511];
        load.image = timg_siz == G_IM_SIZ_16b ? picture_at(timg_addr) : 0;
        if (load.image) {   // from a picture: drawn from its render target, not from RDRAM
            const uint32_t offset = (timg_addr - load.image) / 2;
            load.x = int(offset % timg_width + uls);
            load.y = int(offset / timg_width + ult);
            ++tmem_version;
            return;
        }
        const uint32_t row_bytes = bytes_of(lrs - uls + 1, timg_siz), stride = tile.line * 8;
        for (uint32_t row = 0; row <= lrt - ult; ++row) {
            const uint32_t source = timg_addr + bytes_of((ult + row) * timg_width + uls, timg_siz);
            const uint32_t destination = tile.tmem * 8 + row * stride, swap = (row & 1) ? 4 : 0;
            for (uint32_t i = 0; i < row_bytes; ++i) tmem[((destination + i) ^ swap) & 4095] = rd8(source + i);
        }
        ++tmem_version;
    }

    void load_block(uint32_t w0, uint32_t w1) {
        const Tile& tile = tiles[(w1 >> 24) & 7];
        const uint32_t uls = (w0 >> 12) & 0xFFF, ult = w0 & 0xFFF, lrs = (w1 >> 12) & 0xFFF, dxt = w1 & 0xFFF;
        if (lrs < uls) return;
        picture_loads[tile.tmem & 511].image = 0;
        const uint32_t bytes = (bytes_of(lrs - uls + 1, timg_siz) + 7) & ~7u;
        const uint32_t source = timg_addr + bytes_of(ult * timg_width + uls, timg_siz), destination = tile.tmem * 8;
        for (uint32_t q = 0; q * 8 < bytes; ++q) {
            const uint32_t swap = (((q * dxt) >> 11) & 1) ? 4 : 0;
            for (uint32_t b = 0; b < 8; ++b) tmem[((destination + q * 8 + b) ^ swap) & 4095] = rd8(source + q * 8 + b);
        }
        ++tmem_version;
    }

    void load_tlut(uint32_t w0, uint32_t w1) {
        const Tile& tile = tiles[(w1 >> 24) & 7];
        const uint32_t sl = ((w0 >> 12) & 0xFFF) >> 2, tl = (w0 & 0xFFF) >> 2, sh = ((w1 >> 12) & 0xFFF) >> 2;
        if (sh < sl) return;
        const uint32_t source = timg_addr + (tl * timg_width + sl) * 2;
        for (uint32_t i = 0; i <= sh - sl; ++i) {
            const uint16_t color = rd16(source + i * 2);
            const uint32_t at = (tile.tmem + i) * 8;
            for (uint32_t b = 0; b < 8; b += 2) { tmem[(at + b) & 4095] = uint8_t(color >> 8); tmem[(at + b + 1) & 4095] = uint8_t(color); }
        }
        ++tmem_version;
    }

    // The texture a tile samples: decoded to RGBA8 the first time its contents are seen.
    // Returns its key; width and height and the tile's origin for texture coordinates.
    struct Bound { uint64_t key = 0; uint16_t width = 1, height = 1; float uls = 0, ult = 0; bool linear = false, picture = false; };
    Bound texture(uint8_t index) {
        const Tile& tile = tiles[index];
        const uint32_t tlut = (othermode_h >> 14) & 3;
        const bool linear = ((othermode_h >> 12) & 3) != 0;
        if (const PictureLoad& load = picture_loads[tile.tmem & 511]; load.image) {
            // Texel (s, t) of the tile is pixel (s - uls + x, t - ult + y) of the picture.
            if (load.image == cimg && captured != cimg) {
                Command capture;
                capture.kind = Command::capture;
                capture.state.texture = kPictureKey | load.image;
                frame->commands.push_back(capture);
                captured = cimg;
            }
            return {kPictureKey | load.image, kPictureWidth, kPictureHeight, tile.uls / 4.f - load.x, tile.ult / 4.f - load.y, linear, true};
        }
        const uint32_t tile_w = ((tile.lrs - tile.uls) >> 2) + 1, tile_h = ((tile.lrt - tile.ult) >> 2) + 1;
        const bool wrap_s = !(tile.cms & 2) && tile.masks, wrap_t = !(tile.cmt & 2) && tile.maskt;
        const uint32_t width = std::min<uint32_t>(wrap_s ? 1u << tile.masks : tile_w, 1024);
        const uint32_t height = std::min<uint32_t>(wrap_t ? 1u << tile.maskt : tile_h, 1024);
        uint64_t signature = mix(mix(mix(tile.fmt | tile.siz << 4 | tile.palette << 8 | tile.cms << 12 | tile.cmt << 14 | uint32_t(tile.masks) << 16 | uint32_t(tile.maskt) << 20 | tlut << 24 | uint32_t(linear) << 26,
                                     tile.line | uint32_t(tile.tmem) << 16), width | height << 16), uint64_t(tile.uls) << 32 | tile.ult);
        Memo& memo = this->memo[index];
        Bound bound{0, uint16_t(width), uint16_t(height), tile.uls / 4.f, tile.ult / 4.f, linear};
        if (memo.version == tmem_version && memo.signature == signature) { bound.key = memo.key; return bound; }
        // The TMEM bytes the tile can reach, and the palette for colour-indexed formats.
        uint64_t key = signature;
        const uint32_t row_bytes = tile.line * 8, rows = height;
        const uint32_t span = std::min<uint32_t>(row_bytes * rows, 4096);
        const uint32_t from = tile.tmem * 8;
        if (from + span <= 4096) key = hash_bytes(key, tmem + from, span);
        else { key = hash_bytes(key, tmem + from, 4096 - from); key = hash_bytes(key, tmem, span - (4096 - from)); }
        const bool ci = tile.fmt == G_IM_FMT_CI || (tile.fmt == G_IM_FMT_RGBA && tile.siz <= G_IM_SIZ_8b);
        if (ci) {
            const uint32_t entries = tile.siz == G_IM_SIZ_4b ? 16 : 256, first = tile.siz == G_IM_SIZ_4b ? 256 + tile.palette * 16 : 256;
            key = hash_bytes(key, tmem + first * 8, std::min<uint32_t>(entries * 8, 4096 - first * 8));
        }
        key = (key & 0x00FFFFFFFFFFFFFFull) | 0x0100000000000000ull;   // never 0, never a picture key
        memo = {tmem_version, signature, key, uint16_t(width), uint16_t(height), tile.uls, tile.ult};
        bound.key = key;
        if (!added.count(key) && !known(key)) {
            added.insert(key);
            Frame::Texture texture;
            texture.key = key;
            texture.width = uint16_t(width);
            texture.height = uint16_t(height);
            texture.wrap_s = wrap_s ? ((tile.cms & 1) ? 2 : 1) : 0;
            texture.wrap_t = wrap_t ? ((tile.cmt & 1) ? 2 : 1) : 0;
            texture.linear = linear;
            decode(tile, tlut, width, height, texture.rgba);
            frame->textures.push_back(std::move(texture));
        }
        return bound;
    }

    void decode(const Tile& tile, uint32_t tlut, uint32_t width, uint32_t height, std::vector<uint32_t>& out) const {
        out.resize(size_t(width) * height);
        const uint32_t mask_s = tile.masks ? (1u << tile.masks) - 1 : 0xFFFF, mask_t = tile.maskt ? (1u << tile.maskt) - 1 : 0xFFFF;
        auto palette = [&](uint32_t index) {
            const uint32_t at = (256 + index) * 8;
            const uint16_t c = uint16_t(tmem[at & 4095] << 8 | tmem[(at + 1) & 4095]);
            if (tlut == 3) {   // IA16
                const uint32_t i = c >> 8, a = c & 0xFF;
                return i | i << 8 | i << 16 | a << 24;
            }
            return expand5551(c);
        };
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t row = y & mask_t;
            if (tile.maskt && (tile.cmt & 1) && (y >> tile.maskt) & 1) row = mask_t - row;
            const uint32_t base = tile.tmem * 8 + row * tile.line * 8, swap = (row & 1) ? 4 : 0;
            auto byte = [&](uint32_t offset) { return tmem[((base + offset) ^ swap) & 4095]; };
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t column = x & mask_s;
                if (tile.masks && (tile.cms & 1) && (x >> tile.masks) & 1) column = mask_s - column;
                uint32_t color = 0;
                switch (tile.siz) {
                case G_IM_SIZ_4b: {
                    const uint8_t b = byte(column / 2), n = (column & 1) ? (b & 0xF) : (b >> 4);
                    if (tile.fmt == G_IM_FMT_I) { const uint32_t i = n * 17; color = i | i << 8 | i << 16 | i << 24; }
                    else if (tile.fmt == G_IM_FMT_IA) { const uint32_t i = (n >> 1) * 255 / 7, a = (n & 1) ? 255 : 0; color = i | i << 8 | i << 16 | a << 24; }
                    else color = tlut ? palette(tile.palette * 16 + n) : (n * 17) * 0x01010101u;
                    break;
                }
                case G_IM_SIZ_8b: {
                    const uint8_t b = byte(column);
                    if (tile.fmt == G_IM_FMT_I) color = b * 0x01010101u;
                    else if (tile.fmt == G_IM_FMT_IA) { const uint32_t i = (b >> 4) * 17, a = (b & 0xF) * 17; color = i | i << 8 | i << 16 | a << 24; }
                    else color = tlut ? palette(b) : b * 0x01010101u;
                    break;
                }
                case G_IM_SIZ_16b: {
                    const uint16_t c = uint16_t(byte(column * 2) << 8 | byte(column * 2 + 1));
                    if (tile.fmt == G_IM_FMT_IA) { const uint32_t i = c >> 8, a = c & 0xFF; color = i | i << 8 | i << 16 | a << 24; }
                    else color = expand5551(c);
                    break;
                }
                default: {   // 32-bit: red and green in the low half of TMEM, blue and alpha in the high
                    const uint32_t lo = base + column * 2, hi = lo + 2048;
                    const uint32_t r = tmem[(lo ^ swap) & 4095], g = tmem[((lo + 1) ^ swap) & 4095];
                    const uint32_t b = tmem[(hi ^ swap) & 4095], a = tmem[((hi + 1) ^ swap) & 4095];
                    color = r | g << 8 | b << 16 | a << 24;
                    break;
                }
                }
                out[size_t(y) * width + x] = color;
            }
        }
    }

    // The draw's state from the RDP's modes. `rect`: rectangles take no depth.
    DrawState draw_state(bool textured_allowed, bool rect) {
        DrawState s;
        const uint32_t cycle = (othermode_h >> 20) & 3;
        std::memcpy(s.scissor, scissor, sizeof scissor);
        s.env = env;
        if (cycle == 3) {   // fill: the fill colour, written as it is
            s.shader = 0x3 << 13 | 0x3 << 25 | 0x1F << 8 | 0x7 << 22 | 0xF | 0xF << 4 | 0x7 << 16 | 0x7 << 19;   // D = PRIM, alpha D = PRIM
            return s;
        }
        // The first cycle's fields (the game's two cycles always match; it draws one-cycle).
        const uint32_t a = (combine_w0 >> 20) & 0xF, c = (combine_w0 >> 15) & 0x1F, aa = (combine_w0 >> 12) & 0x7, ac = (combine_w0 >> 9) & 0x7;
        const uint32_t b = (combine_w1 >> 28) & 0xF, d = (combine_w1 >> 15) & 0x7, ab = (combine_w1 >> 12) & 0x7, ad = (combine_w1 >> 9) & 0x7;
        s.shader = a | b << 4 | c << 8 | d << 13 | aa << 16 | ab << 19 | ac << 22 | ad << 25;
        const bool reads_texel = a == 1 || a == 2 || b == 1 || b == 2 || c == 1 || c == 2 || c == 7 || c == 8 || d == 1 || d == 2 ||
                                 aa == 1 || aa == 2 || ab == 1 || ab == 2 || ac == 1 || ac == 2 || ad == 1 || ad == 2;
        if (reads_texel && textured_allowed) s.shader |= shader_textured;
        const uint32_t l = othermode_l;
        const uint32_t p1 = (l >> 30) & 3, a1 = (l >> 26) & 3, m1 = (l >> 22) & 3, b1 = (l >> 18) & 3;
        const bool alpha_blend = (l & FORCE_BL) && p1 == 0 && a1 == 0 && m1 == 1 && b1 == 0;
        s.blend = alpha_blend ? blend_alpha : blend_opaque;
        if ((l & 3) == 1) { s.shader |= shader_threshold; s.alpha_ref = uint8_t(blend); }
        if (!alpha_blend && (l & CVG_X_ALPHA)) s.shader |= shader_cutout;
        if (!rect && (geometry & G_ZBUFFER)) s.depth = ((l & Z_CMP) ? 1 : 0) | ((l & Z_UPD) ? 2 : 0);
        return s;
    }

    void emit(const DrawState& state, const Vertex* v, uint32_t count) {
        if (!is_picture(state.texture)) captured = 0;   // the picture changes: a later read captures again
        auto& commands = frame->commands;
        const uint32_t first = uint32_t(frame->vertices.size());
        frame->vertices.insert(frame->vertices.end(), v, v + count);
        if (!commands.empty() && commands.back().kind == Command::draw && commands.back().state == state &&
            commands.back().first + commands.back().count == first) {
            commands.back().count += count;
            return;
        }
        commands.push_back({Command::draw, state, first, count});
    }

    static void put_color(uint8_t* out, uint32_t rgba) {
        out[0] = uint8_t(rgba >> 24); out[1] = uint8_t(rgba >> 16); out[2] = uint8_t(rgba >> 8); out[3] = uint8_t(rgba);
    }

    void triangle(uint32_t ia, uint32_t ib, uint32_t ic) {
        if (ia >= 64 || ib >= 64 || ic >= 64) return;
        const RspVertex* v[3] = {&vertices[ia], &vertices[ib], &vertices[ic]};
        // Culling in screen space, as the RSP does it.
        if (geometry & (G_CULL_FRONT | G_CULL_BACK)) {
            const float dx1 = v[0]->x / v[0]->w - v[1]->x / v[1]->w, dy1 = v[0]->y / v[0]->w - v[1]->y / v[1]->w;
            const float dx2 = v[2]->x / v[2]->w - v[1]->x / v[1]->w, dy2 = v[2]->y / v[2]->w - v[1]->y / v[1]->w;
            float cross = dx1 * dy2 - dy1 * dx2;
            if ((v[0]->w < 0) ^ (v[1]->w < 0) ^ (v[2]->w < 0)) cross = -cross;
            const uint32_t cull = geometry & (G_CULL_FRONT | G_CULL_BACK);
            if (cull == (G_CULL_FRONT | G_CULL_BACK)) return;
            if (cull == G_CULL_FRONT && cross <= 0) return;
            if (cull == G_CULL_BACK && cross >= 0) return;
        }
        const DrawState state_base = draw_state(texture_on, false);
        DrawState state = state_base;
        Bound bound;
        if (state.shader & shader_textured) { bound = texture(texture_tile); state.texture = bound.key; }
        const Tile& tile = tiles[texture_tile];
        auto shifted = [](float value, uint8_t shift) {
            if (shift == 0) return value;
            return shift <= 10 ? value / float(1 << shift) : value * float(1 << (16 - shift));
        };
        const float half = bound.linear ? 0.5f : 0.f;
        Vertex out[3];
        const bool smooth = geometry & G_SHADING_SMOOTH;
        for (int i = 0; i < 3; ++i) {
            out[i].x = v[i]->x; out[i].y = v[i]->y; out[i].z = v[i]->z; out[i].w = v[i]->w;
            out[i].u = (shifted(v[i]->s, tile.shifts) - bound.uls + half) / bound.width;
            out[i].v = (shifted(v[i]->t, tile.shiftt) - bound.ult + half) / bound.height;
            if (bound.picture) out[i].v = 1 - out[i].v;
            std::memcpy(out[i].shade, smooth ? v[i]->color : v[0]->color, 4);
            put_color(out[i].prim, prim);
        }
        ++frame->triangles;
        emit(state, out, 3);
    }

    // A rectangle in picture pixels (corners), with texel coordinates at its corners.
    void rectangle(const DrawState& state, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
        if (x1 <= x0 || y1 <= y0) return;
        const float X0 = x0 / (kPictureWidth / 2.f) - 1, X1 = x1 / (kPictureWidth / 2.f) - 1;
        const float Y0 = 1 - y0 / (kPictureHeight / 2.f), Y1 = 1 - y1 / (kPictureHeight / 2.f);
        Vertex c[4];
        const float xs[4] = {X0, X1, X0, X1}, ys[4] = {Y0, Y0, Y1, Y1}, us[4] = {u0, u1, u0, u1}, vs[4] = {v0, v0, v1, v1};
        for (int i = 0; i < 4; ++i) {
            c[i].x = xs[i]; c[i].y = ys[i]; c[i].z = 0; c[i].w = 1;
            c[i].u = us[i]; c[i].v = vs[i];
            std::memset(c[i].shade, 0, 4);
            put_color(c[i].prim, color);
        }
        const Vertex six[6] = {c[0], c[1], c[2], c[2], c[1], c[3]};
        emit(state, six, 6);
    }

    // A rectangle's corners in pixels after the extended rectangle alignment.
    void aligned(uint32_t ulx4, uint32_t uly4, uint32_t lrx4, uint32_t lry4, float& ulx, float& uly, float& lrx, float& lry) const {
        ulx = from_origin(int32_t(ulx4) + rect_offset[0], rect_origin[0]) / 4.f;
        uly = (int32_t(uly4) + rect_offset[1]) / 4.f;
        lrx = from_origin(int32_t(lrx4) + rect_offset[2], rect_origin[1]) / 4.f;
        lry = (int32_t(lry4) + rect_offset[3]) / 4.f;
    }

    void texture_rectangle(uint32_t w0, uint32_t w1, bool flip) {
        float ulx, uly, lrx, lry;
        aligned((w1 >> 12) & 0xFFF, w1 & 0xFFF, (w0 >> 12) & 0xFFF, w0 & 0xFFF, ulx, uly, lrx, lry);
        const uint8_t tile = (w1 >> 24) & 7;
        const float s = int16_t(half1 >> 16) / 32.f, t = int16_t(half1) / 32.f;
        float dsdx = int16_t(half2 >> 16) / 1024.f, dtdy = int16_t(half2) / 1024.f;
        const uint32_t cycle = (othermode_h >> 20) & 3;
        float right = lrx, bottom = lry;
        if (cycle >= 2) { right += 1; bottom += 1; if (cycle == 2) dsdx /= 4; }   // copy and fill include the far edge
        DrawState state = draw_state(true, true);
        Bound bound;
        if (state.shader & shader_textured) { bound = texture(tile); state.texture = bound.key; }
        // GL samples at pixel centres; the RDP samples the first pixel at (s, t) exactly,
        // with texel centres on whole numbers when it filters.
        const float half = bound.linear ? 0.5f : 0.f;
        const float w = right - ulx, h = bottom - uly;
        float u0 = s - bound.uls - 0.5f * dsdx + half, v0 = t - bound.ult - 0.5f * dtdy + half;
        float u1 = u0 + (flip ? h : w) * dsdx, v1 = v0 + (flip ? w : h) * dtdy;
        if (flip) { std::swap(u0, v0); std::swap(u1, v1); }   // not used by the game
        ++frame->rectangles;
        v0 /= bound.height; v1 /= bound.height;
        if (bound.picture) { v0 = 1 - v0; v1 = 1 - v1; }
        rectangle(state, ulx, uly, right, bottom, u0 / bound.width, v0, u1 / bound.width, v1, prim);
    }

    void fill_rectangle(uint32_t w0, uint32_t w1) {
        float ulx, uly, lrx, lry;
        aligned((w1 >> 12) & 0xFFF, w1 & 0xFFF, (w0 >> 12) & 0xFFF, w0 & 0xFFF, ulx, uly, lrx, lry);
        const uint32_t cycle = (othermode_h >> 20) & 3;
        if (cycle == 3) {
            if (cimg == zimg) {   // the Z buffer cleared
                Command clear;
                clear.kind = Command::clear_depth;
                frame->commands.push_back(clear);
                ++frame->depth_clears;
                return;
            }
            lrx += 1; lry += 1;
            // The fill colour as the 16-bit picture holds it (both halves the same pixel).
            const uint32_t c = expand5551(uint16_t(fill >> 16));
            const uint32_t rgba = (c & 0xFF) << 24 | ((c >> 8) & 0xFF) << 16 | ((c >> 16) & 0xFF) << 8 | 0xFF;
            ++frame->fills;
            rectangle(draw_state(false, true), ulx, uly, lrx, lry, 0, 0, 0, 0, rgba);
            return;
        }
        ++frame->fills;
        rectangle(draw_state(false, true), ulx, uly, lrx, lry, 0, 0, 0, 0, prim);
    }

    void run(uint32_t start) {
        uint32_t stack[18];
        int call_depth = 0;
        uint32_t cursor = start & 0x1FFFFFFF;
        for (uint32_t guard = 0; guard < 1000000 && cursor + 8 <= kRam; ++guard) {
            const uint32_t w0 = rd32(cursor), w1 = rd32(cursor + 4);
            const uint8_t op = uint8_t(w0 >> 24);
            if (log && (op == G_SETTIMG || op == G_SETTILE || op == G_SETTILESIZE || op == G_LOADBLOCK || op == G_LOADTILE ||
                        op == G_LOADTLUT || op == G_TEXRECT || op == G_SETOTHERMODE_H || op == G_SETCOMBINE))
                std::fprintf(log, "    %08X: %08X %08X\n", cursor, w0, w1);
            cursor += 8;
            switch (op) {
            case G_VTX: vertex(w0, w1); break;
            case G_TRI1: case G_TRI2: case G_QUAD: {
                const uint32_t indices[6] = {((w0 >> 16) & 0xFF) / 2, ((w0 >> 8) & 0xFF) / 2, (w0 & 0xFF) / 2,
                                             ((w1 >> 16) & 0xFF) / 2, ((w1 >> 8) & 0xFF) / 2, (w1 & 0xFF) / 2};
                const int count = op == G_TRI1 ? 3 : 6;
                if (const uint32_t id = tagged()) {
                    float box[4] = {1e9f, 1e9f, -1e9f, -1e9f};
                    for (int i = 0; i < count; ++i) {
                        const RspVertex& v = vertices[indices[i] & 63];
                        if (v.w <= 0) continue;
                        const float x = (v.x / v.w + 1) * kPictureWidth / 2, y = (1 - v.y / v.w) * kPictureHeight / 2;
                        box[0] = std::min(box[0], x); box[1] = std::min(box[1], y); box[2] = std::max(box[2], x); box[3] = std::max(box[3], y);
                    }
                    native(id, box[0], box[1], box[2], box[3]);
                    break;
                }
                triangle(indices[0], indices[1], indices[2]);
                if (count == 6) triangle(indices[3], indices[4], indices[5]);
                break;
            }
            case G_TEXTURE:
                texture_s = uint16_t(w1 >> 16); texture_t = uint16_t(w1);
                texture_tile = (w0 >> 8) & 7;
                texture_on = ((w0 >> 1) & 0x7F) != 0;
                break;
            case G_POPMTX: {
                const int count = int(w1 / 64);
                depth = std::max(0, depth - count);
                mvp_dirty = true;
                break;
            }
            case G_GEOMETRYMODE: geometry = (geometry & (w0 & 0x00FFFFFF)) | w1; break;
            case G_MTX: matrix(w0, w1); break;
            case G_MOVEWORD: moveword(w0, w1); break;
            case G_MOVEMEM: movemem(w0, w1); break;
            case G_DL: {
                const uint32_t target = segmented(w1);
                if (((w0 >> 16) & 0xFF) == 0) {
                    if (call_depth >= 18) return;
                    stack[call_depth++] = cursor;
                }
                cursor = target;
                break;
            }
            case G_ENDDL:
                if (call_depth == 0) return;
                cursor = stack[--call_depth];
                break;
            case G_RDPHALF_1: half1 = w1; break;
            case G_RDPHALF_2: half2 = w1; break;
            case G_SETOTHERMODE_H: case G_SETOTHERMODE_L: {
                const uint32_t length = (w0 & 0xFF) + 1, shift = 32 - ((w0 >> 8) & 0xFF) - length;
                const uint32_t mask = (length >= 32 ? 0xFFFFFFFFu : ((1u << length) - 1)) << shift;
                uint32_t& mode = op == G_SETOTHERMODE_H ? othermode_h : othermode_l;
                mode = (mode & ~mask) | (w1 & mask);
                break;
            }
            case G_RDPSETOTHERMODE: othermode_h = w0 & 0x00FFFFFF; othermode_l = w1; break;
            case G_TEXRECT: case G_TEXRECTFLIP: {
                // F3DEX2 sends the rectangle's texture coordinates in the next two commands.
                half1 = rd32(cursor + 4);
                half2 = rd32(cursor + 12);
                cursor += 16;
                if (const uint32_t id = tagged()) {
                    float ulx, uly, lrx, lry;
                    aligned((w1 >> 12) & 0xFFF, w1 & 0xFFF, (w0 >> 12) & 0xFFF, w0 & 0xFFF, ulx, uly, lrx, lry);
                    native(id, ulx, uly, lrx, lry);
                    break;
                }
                texture_rectangle(w0, w1, op == G_TEXRECTFLIP);
                break;
            }
            case G_SETSCISSOR:
                scissor[0] = int16_t(((w0 >> 12) & 0xFFF) / 4); scissor[1] = int16_t((w0 & 0xFFF) / 4);
                scissor[2] = int16_t(((w1 >> 12) & 0xFFF) / 4); scissor[3] = int16_t((w1 & 0xFFF) / 4);
                break;
            case G_LOADTLUT: load_tlut(w0, w1); break;
            case G_SETTILESIZE: {
                Tile& tile = tiles[(w1 >> 24) & 7];
                tile.uls = (w0 >> 12) & 0xFFF; tile.ult = w0 & 0xFFF; tile.lrs = (w1 >> 12) & 0xFFF; tile.lrt = w1 & 0xFFF;
                break;
            }
            case G_LOADBLOCK: load_block(w0, w1); break;
            case G_LOADTILE: {
                load_tile(w0, w1);
                Tile& tile = tiles[(w1 >> 24) & 7];   // a load sets the tile's size too
                tile.uls = (w0 >> 12) & 0xFFF; tile.ult = w0 & 0xFFF; tile.lrs = (w1 >> 12) & 0xFFF; tile.lrt = w1 & 0xFFF;
                break;
            }
            case G_SETTILE: {
                Tile& tile = tiles[(w1 >> 24) & 7];
                tile.fmt = (w0 >> 21) & 7; tile.siz = (w0 >> 19) & 3; tile.line = (w0 >> 9) & 0x1FF; tile.tmem = w0 & 0x1FF;
                tile.palette = (w1 >> 20) & 0xF; tile.cmt = (w1 >> 18) & 3; tile.maskt = (w1 >> 14) & 0xF; tile.shiftt = (w1 >> 10) & 0xF;
                tile.cms = (w1 >> 8) & 3; tile.masks = (w1 >> 4) & 0xF; tile.shifts = w1 & 0xF;
                break;
            }
            case G_FILLRECT: fill_rectangle(w0, w1); break;
            case G_SETFILLCOLOR: fill = w1; break;
            case G_SETFOGCOLOR: fog = w1; break;
            case G_SETBLENDCOLOR: blend = w1; break;
            case G_SETPRIMCOLOR: prim = w1; break;
            case G_SETENVCOLOR: env = w1; break;
            case G_SETCOMBINE: combine_w0 = w0 & 0x00FFFFFF; combine_w1 = w1; break;
            case G_SETTIMG:
                timg_fmt = (w0 >> 21) & 7; timg_siz = (w0 >> 19) & 3; timg_width = (w0 & 0xFFF) + 1;
                timg_addr = segmented(w1);
                break;
            case G_SETZIMG: zimg = segmented(w1); break;
            case G_SETCIMG:
                cimg = segmented(w1);
                if (cimg != zimg) {
                    frame->color_image = cimg;
                    if (std::find(std::begin(pictures), std::end(pictures), cimg) == std::end(pictures)) {
                        std::move(pictures, pictures + 3, pictures + 1);
                        pictures[0] = cimg;
                    }
                }
                break;
            case G_EXTENDED: {
                const uint32_t sub = w0 & 0x00FFFFFF;
                const int words = extended_words(sub);
                if (words == 0) return;
                const uint32_t w2 = rd32(cursor), w3 = rd32(cursor + 4);
                cursor += uint32_t(words - 2) * 4;
                switch (sub) {
                case 0x05: {   // G_EX_SETSCISSOR: left and right origins, corners
                    const uint32_t left = (w1 >> 2) & 0xFFF, right = (w1 >> 14) & 0xFFF;
                    scissor[0] = int16_t(from_origin(int16_t(w2 >> 16), left) / 4);
                    scissor[1] = int16_t(int16_t(w2) / 4);
                    scissor[2] = int16_t(from_origin(int16_t(w3 >> 16), right) / 4);
                    scissor[3] = int16_t(int16_t(w3) / 4);
                    break;
                }
                case 0x06:     // G_EX_SETRECTALIGN: origins, then offsets
                    rect_origin[0] = w1 & 0xFFF; rect_origin[1] = (w1 >> 12) & 0xFFF;
                    rect_offset[0] = int16_t(w2 >> 16); rect_offset[1] = int16_t(w2);
                    rect_offset[2] = int16_t(w3 >> 16); rect_offset[3] = int16_t(w3);
                    break;
                case 0x17:     // G_EX_PUSHSCISSOR
                    if (scissor_depth < 8) std::memcpy(scissor_stack[scissor_depth++], scissor, sizeof scissor);
                    break;
                case 0x18:     // G_EX_POPSCISSOR
                    if (scissor_depth > 0) std::memcpy(scissor, scissor_stack[--scissor_depth], sizeof scissor);
                    break;
                default: break;   // the aspect hint and the rest change nothing in a 4:3 picture
                }
                break;
            }
            default: break;   // syncs, no-ops, culling and the commands the game never sends
            }
            tag_w0 = w0; tag_w1 = w1;
        }
    }
};

Interpreter::Interpreter(std::function<bool(uint64_t)> known) : state(new State) {
    state->known = std::move(known);
    for (auto& m : state->modelview) m = identity();
}

Interpreter::~Interpreter() { delete state; }

void Interpreter::trace(std::FILE* into) { state->log = into; }

void Interpreter::classify(Classify hook) { state->classify = std::move(hook); }

void Interpreter::run(const uint8_t* rdram, uint32_t start, Frame& out) {
    state->rdram = rdram;
    state->frame = &out;
    state->added.clear();
    state->captured = 0;
    state->run(start);
    state->frame = nullptr;
}

}  // namespace srw64::n64gl
