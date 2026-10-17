// The N64 picture without RT64, for the PS Vita port (docs/design/psv-port.md, V1): an
// F3DEX2 display list interpreter for the subset this game uses (docs/data/gbi-usage.md:
// one-cycle combiners, CI4/CI8/RGBA16 textures, texture and fill rectangles, lit and unlit
// triangles, the Z buffer) that turns a graphics task into batched vertices and draw
// commands. It knows nothing of GL: gl_renderer.hpp draws a Frame on desktop OpenGL 2.1
// and on the Vita's vitaGL alike, and the interpreter itself is plain C++.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace srw64::n64gl {

// The picture the game draws into: the colour image is always 320 wide (gbi-usage.md).
constexpr int kPictureWidth = 320, kPictureHeight = 240;

struct Vertex {
    float x, y, z, w;      // clip space of the 320x240 picture, GL's convention (y up)
    float u, v;            // normalised texture coordinates
    uint8_t shade[4];      // the RSP's vertex colour (lit or not)
    uint8_t prim[4];       // the primitive colour at the draw; fill colour for fills
};

enum Blend : uint8_t { blend_opaque = 0, blend_alpha = 1 };

// What a draw needs besides its vertices; equal states draw as one batch.
struct DrawState {
    uint32_t shader = 0;      // shader_key(): the combiner and the fragment's discard rules
    uint64_t texture = 0;     // a Frame::Texture key, 0 when the combiner reads no texel
    uint32_t env = 0;         // environment colour, RGBA
    int16_t scissor[4] = {};  // left, top, right, bottom in picture pixels
    uint8_t blend = blend_opaque;
    uint8_t depth = 0;        // bit 0 test, bit 1 write
    uint8_t alpha_ref = 0;    // the blend colour's alpha: alpha compare's threshold
    bool operator==(const DrawState&) const = default;
};

// The shader key: the first cycle's combiner fields and the alpha rules.
enum : uint32_t {
    shader_combiner = 0x0FFFFFFF,
    shader_threshold = 1u << 28,   // alpha compare: discard below alpha_ref (and at 0)
    shader_cutout = 1u << 29,      // coverage times alpha, not blended: discard below one half
    shader_textured = 1u << 30,    // the combiner reads TEXEL0
};
struct Combiner { uint8_t a, b, c, d, aa, ab, ac, ad; };
Combiner combiner_of(uint32_t shader);

// Texture keys that name a colour image instead of decoded TMEM: the picture the game drew
// there (an earlier frame's, read back for after-images and the title's speed lines).
// The draw's v runs up the picture, as GL keeps a render target.
constexpr uint64_t kPictureKey = 0xFB00000000000000ull;
inline bool is_picture(uint64_t key) { return (key >> 56) == 0xFB; }
inline uint32_t picture_address(uint64_t key) { return uint32_t(key); }

struct Command {
    // capture: the picture drawn so far into the colour image it names (state.texture), for
    // the draws after it that read the image being drawn.
    // native: a draw the host paints itself (a window frame, story text): the rectangle or
    // triangles a tag marked (native_map.cpp and the others write a G_NOOP before them),
    // with its id, where it was and the scissor; the game's own pixels are not drawn.
    enum Kind : uint8_t { draw, clear_depth, capture, native } kind = draw;
    DrawState state;
    uint32_t first = 0, count = 0;   // vertices, three per triangle
    uint32_t native_id = 0;
    float rect[4] = {};              // native: left, top, right, bottom in picture pixels
    // native, from triangles: model space to the picture's clip space (row vectors, the
    // viewport folded in, as the interpreter's own vertices) and the game's depth test and
    // write for the draw (bit 0, bit 1).
    bool model = false;
    float mvp[16] = {};
    uint8_t depth = 0;
};

// The host's tags: the command before a draw, a G_NOOP whose words name it. Returns the
// draw's native id, 0 for an ordinary draw.
using Classify = std::function<uint32_t(uint32_t tag_w0, uint32_t tag_w1)>;

struct Frame {
    std::vector<Vertex> vertices;
    std::vector<Command> commands;
    // Textures this frame uses that the renderer did not have: decoded to RGBA8.
    struct Texture {
        uint64_t key = 0;
        uint16_t width = 0, height = 0;
        uint8_t wrap_s = 0, wrap_t = 0;   // 0 clamp, 1 repeat, 2 mirror
        bool linear = false;
        std::vector<uint32_t> rgba;       // bytes R, G, B, A in memory order
    };
    std::vector<Texture> textures;
    uint32_t color_image = 0;              // where the frame's colour image is (the VI origin)
    uint32_t triangles = 0, rectangles = 0, fills = 0, depth_clears = 0, natives = 0;
    void clear() {
        vertices.clear(); commands.clear(); textures.clear();
        color_image = 0; triangles = rectangles = fills = depth_clears = natives = 0;
    }
};

class Interpreter {
public:
    // `known(key)` says whether the renderer already holds a texture, so it is not decoded again.
    explicit Interpreter(std::function<bool(uint64_t)> known);
    ~Interpreter();
    // One graphics task: RDRAM as the recompiled runtime keeps it (8 MiB of 32-bit words in
    // host byte order), the display list at the physical address `start`. The RSP and RDP
    // state carries over from task to task, as on the console. With `list`, the top-level
    // list's `size` bytes are read from there instead (indexed by physical address, as the
    // host's edited copy is: the native dialogue takes the original text out of it).
    void run(const uint8_t* rdram, uint32_t start, Frame& out, const uint8_t* list = nullptr, uint32_t size = 0);
    // The host's tagged draws (Command::native); none by default.
    void classify(Classify hook);
    // Print the texture loads and tile settings as they run (a development aid), or stop.
    void trace(std::FILE* into);
private:
    struct State;
    State* state;
};

}  // namespace srw64::n64gl
