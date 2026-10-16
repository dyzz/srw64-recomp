// The V0 headless probe host (docs/design/psv-port.md): the recompiled game, the runtime
// and the RSP audio microcode with no picture, sound or input, run for a number of game
// frames from power-on and then stopped with a snapshot of the game's 8 MiB RDRAM. The
// same program built for the machine's own CPU and for 32-bit ARM under qemu-arm gives
// two snapshots to compare (build_probe.py), which is what tells whether the generated
// code is right in ILP32.
//
// Every native hook of the game host (generate_cpu.py NATIVE_HOOKS) calls its original
// here (probe_hooks.cpp, generated), except the frame boundary below; the host's map
// width and render node rewrites take their 4:3 values. The game therefore runs as the
// original ROM does, with no host features.
//
//   srw64-vita-probe ROM OUTPUT_DIR FRAMES [MAX_VIS]
//
// Writes OUTPUT_DIR/rdram.bin (8 MiB, at the start of game frame FRAMES, taken on the game
// thread) and OUTPUT_DIR/counters.json. MAX_VIS (default 10 x FRAMES, at 60 per second)
// bounds a run whose game never reaches that frame.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/error_handling.hpp"
#include "ultramodern/renderer_context.hpp"
#include "funcs.h"
#include "recomp_overlays.inl"

RspExitReason srw64_audio_probe(uint8_t*, uint32_t);

namespace {
std::atomic<uint64_t> vi_count{}, dl_count{}, audio_tasks{}, frames{};
std::filesystem::path output_dir;
uint64_t max_frames = 300, max_vis = 0;
std::chrono::steady_clock::time_point started;
std::atomic_bool snapshot_written{};
std::mutex vi_mutex;
std::condition_variable vi_credit;
uint64_t vi_credits = 0, vi_fallbacks = 0;

[[noreturn]] void fail(const char* text) {
    std::fprintf(stderr, "SRW64_PROBE_FAILURE %s\n", text);
    std::fflush(stderr);
    std::abort();
}

class NullRenderer final : public ultramodern::renderer::RendererContext {
public:
    NullRenderer() {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
    }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask*) override { ++dl_count; }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};

void write_counters(const char* reason) {
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ofstream report(output_dir / "counters.json");
    report << "{\"schema\":\"srw64.vita-probe-counters.v1\",\"reason\":\"" << reason
           << "\",\"frames\":" << frames.load() << ",\"vis\":" << vi_count.load()
           << ",\"graphics_tasks\":" << dl_count.load() << ",\"audio_tasks\":" << audio_tasks.load()
           << ",\"vi_fallbacks\":" << vi_fallbacks
           << ",\"seconds\":" << seconds << ",\"pointer_bits\":" << (sizeof(void*) * 8) << "}\n";
}

void on_vi() {
    const auto vi = ++vi_count;
    if (vi % 300 == 0) {
        std::fprintf(stderr, "SRW64_PROBE vi=%llu frames=%llu lists=%llu\n", (unsigned long long)vi,
                     (unsigned long long)frames.load(), (unsigned long long)dl_count.load());
    }
    if (max_vis && vi >= max_vis) ultramodern::quit();
}

bool get_input(int port, uint16_t* buttons, float* x, float* y) {
    if (port != 0) return false;
    *buttons = 0;
    *x = *y = 0;
    return true;
}

ultramodern::input::connected_device_info_t get_device(int port) {
    using namespace ultramodern::input;
    return {port == 0 ? Device::Controller : Device::None, Pak::None};
}

std::vector<size_t> loaded_sections;

// As the game host's on_init (src/host/host.cpp): the runtime registered 1 MiB at the
// entrypoint, but only the resident section lives there; the game's own loader brings in
// the overlays (the ROM read hook below). The guest globals osInitialize set stay as the
// host leaves them (the CPU clock at 3/4, the VI clock by TV type).
void on_init(uint8_t* rdram, recomp_context*) {
    unload_overlays((int32_t)0x80076610, 0x100000);
    load_overlays(0x1000, (int32_t)0x80076610, 0x5AC30);
    loaded_sections = {0};
    const uint64_t clock = ((uint64_t)(uint32_t)MEM_W(0, (int32_t)0x800CEBD0) << 32) |
                           (uint32_t)MEM_W(0, (int32_t)0x800CEBD4);
    const uint64_t adjusted = clock * 3 / 4;
    MEM_W(0, (int32_t)0x800CEBD0) = adjusted >> 32;
    MEM_W(0, (int32_t)0x800CEBD4) = adjusted;
    const uint32_t tv_type = MEM_W(0, (int32_t)0x80000300);
    MEM_W(0, (int32_t)0x800CEBD8) = tv_type == 0 ? 0x02F5B2D2 : tv_type == 2 ? 0x02E6025C : 0x02E6D354;
}

// The VI thread's pacing (probe_events.cpp, written by build_probe.py from ultramodern's
// events.cpp): a VI for every frame boundary the game passes, so the two builds compared
// see the same VIs, audio tasks and display lists, and never faster than 60 Hz; where the
// game is not frame-paced (boot, loads) a VI every 200 ms (100 ms once paced) keeps it moving.
RspUcodeFunc* get_microcode(const OSTask* task) {
    if (task->t.type != 2 || (uint32_t)task->t.ucode != 0x800C4910 ||
        (uint32_t)task->t.ucode_data != 0x800CF4D0) return nullptr;
    ++audio_tasks;
    return srw64_audio_probe;
}
}  // namespace

void srw64_probe_wait_vi() {
    using namespace std::chrono;
    static steady_clock::time_point last = steady_clock::now();
    static bool paced = false;
    {
        std::unique_lock lock(vi_mutex);
        // Never ahead of the game once it is frame-paced; before that (boot), a VI every 200 ms.
        if (vi_credit.wait_for(lock, paced ? milliseconds(100) : milliseconds(200), [] { return vi_credits > 0; })) {
            --vi_credits;
            paced = true;
        } else {
            ++vi_fallbacks;
        }
    }
    // Never faster than 60 Hz, as the runtime's own VI thread.
    const auto next = last + microseconds(16667);
    std::this_thread::sleep_until(next);
    last = std::max(next, steady_clock::now() - microseconds(2000));
}

// The frame boundary (80085F30, where the game host runs its per-frame hooks): counted on
// the game thread, and at frame FRAMES the snapshot, before the original runs, so every
// build stops at the same point of the game's own logic.
extern "C" void resident_func_80085F30(uint8_t* rdram, recomp_context* ctx) {
    const uint64_t frame = ++frames;
    {
        std::lock_guard lock(vi_mutex);
        ++vi_credits;
    }
    vi_credit.notify_one();
    if (frame == max_frames && !snapshot_written.exchange(true)) {
        std::ofstream snapshot(output_dir / "rdram.bin", std::ios::binary);
        snapshot.write(reinterpret_cast<const char*>(rdram), 8 * 1024 * 1024);
        snapshot.close();
        write_counters("frames");
        std::fprintf(stderr, "SRW64_PROBE_SNAPSHOT frame=%llu vi=%llu lists=%llu audio=%llu\n",
                     (unsigned long long)frame, (unsigned long long)vi_count.load(),
                     (unsigned long long)dl_count.load(), (unsigned long long)audio_tasks.load());
        std::fflush(stderr);
        std::_Exit(0);
    }
    srw64_original_frame_boundary(rdram, ctx);
}

// The ROM read (8007F704): a DMA that lands a code section swaps the recompiled overlays
// with it, as the game host does (without its content patches).
extern "C" void resident_func_8007F704(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t rom = ctx->r4, ram = ctx->r5, size = ctx->r6;
    srw64_original_rom_read(rdram, ctx);
    for (size_t index = 1; index < num_sections; ++index) {
        const auto& section = section_table[index];
        if (section.rom_addr != rom || section.size != size || (uint32_t)section.ram_addr != ram) continue;
        for (auto it = loaded_sections.begin(); it != loaded_sections.end();) {
            const auto& old = section_table[*it];
            const uint64_t start = (uint32_t)old.ram_addr, end = start + old.size;
            if ((uint64_t)ram < end && (uint64_t)ram + size > start) {
                unload_overlays(old.ram_addr, old.size);
                it = loaded_sections.erase(it);
            } else ++it;
        }
        load_overlays(rom, (int32_t)ram, size);
        loaded_sections.push_back(index);
        return;
    }
}

// The game host's tactical-map width rewrites (generate_cpu.py MAP_WIDTH_CONSTANTS) and the
// render node call (RENDER_NODE_CALLS), at their original 4:3 values.
extern "C" int32_t srw64_map_x(int32_t original, int32_t) { return original; }
extern "C" int32_t srw64_map_x_float(uint32_t bits, int32_t) { return int32_t(bits); }
extern "C" int32_t srw64_map_anchor_scroll(int32_t scroll, float) { return scroll; }
extern "C" void srw64_render_node(uint8_t* rdram, recomp_context* ctx, int32_t function, int32_t) {
    LOOKUP_FUNC(function)(rdram, ctx);
}

int main(int argc, char** argv) {
    if (argc < 4 || argc > 5) {
        std::fprintf(stderr, "Usage: srw64-vita-probe ROM OUTPUT_DIR FRAMES [MAX_VIS]\n");
        return 2;
    }
    output_dir = std::filesystem::absolute(argv[2]);
    max_frames = std::strtoull(argv[3], nullptr, 10);
    max_vis = argc == 5 ? std::strtoull(argv[4], nullptr, 10) : max_frames * 10;
    if (max_frames == 0) fail("FRAMES must be positive");
    std::filesystem::create_directories(output_dir / "runtime-data");
    started = std::chrono::steady_clock::now();
    std::fprintf(stderr, "SRW64_PROBE pointer_bits=%u frames=%llu max_vis=%llu\n", unsigned(sizeof(void*) * 8),
                 (unsigned long long)max_frames, (unsigned long long)max_vis);

    recomp::register_config_path(output_dir / "runtime-data");
    recomp::overlays::register_overlays({section_table, num_sections, num_sections},
                                        {overlay_sections_by_index, std::size(overlay_sections_by_index)});
    recomp::GameEntry game{};
    game.rom_hash = 0xbb8703e9d7c77f4full;  // the JP ROM (config/recomp/rom-variants.json xxh3_64)
    game.internal_name = "\xBD\xB0\xCA\xDF\xB0\xDB\xCE\xDE\xAF\xC4\xC0\xB2\xBE\xDD" "64    ";
    game.display_name = "Super Robot Wars 64 V0 probe";
    game.game_id = u8"srw64-jp-rev0";
    game.mod_game_id = "srw64";  // the runtime's command line names games by this id
    game.save_type = recomp::SaveType::Sram;
    game.is_enabled = true;
    game.entrypoint_address = (int32_t)0x80076610;
    game.entrypoint = recomp_entrypoint;
    game.on_init_callback = on_init;
    if (!recomp::register_game(game)) fail("game registration failed");
    const auto validation = recomp::select_rom(argv[1], game.game_id);
    if (validation != recomp::RomValidationError::Good) {
        std::fprintf(stderr, "SRW64_PROBE_ROM validation=%d\n", int(validation));
        fail("ROM validation failed");
    }
    recomp::check_all_stored_roms();
    if (!recomp::is_rom_valid(game.game_id)) fail("stored ROM readback failed");

    char game_option[] = "--game", game_name[] = "srw64";
    char* runtime_args[] = {argv[0], game_option, game_name};
    recomp::Configuration cfg{};
    cfg.argc = 3;
    cfg.argv = runtime_args;
    cfg.project_version = {0, 0, 1, "vita-probe"};
    cfg.gfx_callbacks.create_window = [](ultramodern::gfx_callbacks_t::gfx_data_t) { return ultramodern::renderer::WindowHandle{}; };
    cfg.renderer_callbacks.create_render_context = [](uint8_t*, ultramodern::renderer::WindowHandle, bool)
        -> std::unique_ptr<ultramodern::renderer::RendererContext> { return std::make_unique<NullRenderer>(); };
    cfg.rsp_callbacks.get_rsp_microcode = get_microcode;
    cfg.audio_callbacks.queue_samples = [](int16_t*, size_t) {};
    cfg.audio_callbacks.get_frames_remaining = []() -> size_t { return 0; };
    cfg.audio_callbacks.set_frequency = [](uint32_t) {};
    cfg.input_callbacks.get_input = get_input;
    cfg.input_callbacks.get_connected_device_info = get_device;
    cfg.events_callbacks.vi_callback = on_vi;
    cfg.error_handling_callbacks.message_box = [](const char* message) { std::fprintf(stderr, "SRW64_RUNTIME_ERROR %s\n", message); };
    recomp::start(cfg);
    write_counters("vis");
    std::fprintf(stderr, "SRW64_PROBE_END frames=%llu vi=%llu lists=%llu\n", (unsigned long long)frames.load(),
                 (unsigned long long)vi_count.load(), (unsigned long long)dl_count.load());
    return snapshot_written ? 0 : 3;
}
