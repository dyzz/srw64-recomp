#include "state_probe.hpp"
#include "app/runtime.hpp"
// Native integration host, optionally rendered through RT64/Metal.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "funcs.h"
#include "recomp_overlays.inl"
#include "rom_variants.hpp"
#include "diagnostics.hpp"
#include "script_inject.hpp"
#include "mini_stage.hpp"
#include "rule_fixes.hpp"
#include "upgrade_refund.hpp"
#if defined(SRW64_WITH_RT64)
#include "graphics.hpp"
#include "app/launch.hpp"
#include "app/desktop.hpp"
#include "audio.hpp"
#include "native_dialogue.hpp"
#include "native_intro.hpp"
#include "native_name_entry.hpp"
#include "link_page.hpp"
#include "battle_page.hpp"
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
#include "parts_page.hpp"
#include "ability_page.hpp"
#include "settings_window.hpp"
#include "debug_server.hpp"
#endif

RspExitReason srw64_audio_probe(uint8_t*, uint32_t);

namespace {
std::atomic<uint64_t> vi_count{}, dl_count{}, audio_tasks{}, audio_samples{};
std::atomic<uint32_t> frequency{};
std::filesystem::path output_dir;
uint64_t max_vis = 600;
std::vector<size_t> loaded_sections;
std::vector<uint16_t> input_buttons;
std::atomic<uint64_t> live_buttons{}; // Upper bits: exclusive end VI; lower 16: N64 mask.
uint64_t last_control_sequence{};
std::atomic_bool control_quit{};

[[noreturn]] void fail(const char* text) {
    std::fprintf(stderr, "SRW64_HOST_FAILURE %s\n", text);
    std::abort();
}

class RecordingRenderer final : public ultramodern::renderer::RendererContext {
public:
    explicit RecordingRenderer(uint8_t* rdram, std::unique_ptr<ultramodern::renderer::RendererContext> renderer = {})
        : memory(rdram), renderer(std::move(renderer)) {
        setup_result = this->renderer ? this->renderer->get_setup_result() : ultramodern::renderer::SetupResult::Success;
        chosen_api = this->renderer ? this->renderer->get_chosen_api() : ultramodern::renderer::GraphicsApi::Auto;
    }
    bool valid() override { return !renderer || renderer->valid(); }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        if (renderer) renderer->send_dl(task);
        const auto count = ++dl_count;
        if (count <= 3 || count % 60 == 0) {
            std::fprintf(stderr, "SRW64_GFX task=%llu ucode=%08X commands=%08X size=%u\n",
                         (unsigned long long)count, (uint32_t)task->t.ucode,
                         (uint32_t)task->t.data_ptr, task->t.data_size);
        }
        if (srw64_full_diagnostics() && (count == 1 || count % 120 == 0)) {
            std::vector<uint8_t> bytes(0x800000);
            for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = memory[i ^ 3];
            std::ofstream snapshot(output_dir / "latest-gfx-rdram.bin", std::ios::binary);
            snapshot.write((const char*)bytes.data(), bytes.size());
            std::ofstream description(output_dir / "latest-gfx-task.bin", std::ios::binary);
            description.write((const char*)task, sizeof(*task));
        }
    }
    void send_dummy_workload(uint32_t address) override { if (renderer) renderer->send_dummy_workload(address); }
    void update_screen() override { if (renderer) renderer->update_screen(); }
    void shutdown() override { if (renderer) renderer->shutdown(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
private:
    uint8_t* memory;
    std::unique_ptr<ultramodern::renderer::RendererContext> renderer;
};

void on_init(uint8_t* rdram, recomp_context*) {
    srw64::state_probe::directory=output_dir;
    srw64::mini_stage::configure(output_dir);
    srw64::rules::configure(output_dir);
    const auto rom=recomp::get_rom();
    srw64::upgrades::initialize(rom.data(),rom.size());
    srw64::upgrades::configure(output_dir);
    srw64::refund::configure(output_dir);
#if defined(SRW64_WITH_RT64)
    srw64::names::initialize_rom(rom.data(),rom.size());
#endif
    // The generic runtime initially loads/registers 1 MiB. Only the resident
    // section belongs at this entrypoint; the game's own loader owns overlays.
    unload_overlays((int32_t)0x80076610, 0x100000);
    load_overlays(0x1000, (int32_t)0x80076610, 0x5AC30);
    loaded_sections = {0};
    // The runtime has already copied the resident section; a rules file's table
    // changes go in before any guest code runs.
    srw64::upgrades::patch_resident(rdram);

    // Guest globals initialized by this ROM's osInitialize. The host runtime
    // initializes its own services, so preserve these ROM-visible side effects.
    const uint64_t clock = ((uint64_t)(uint32_t)MEM_W(0, (int32_t)0x800CEBD0) << 32) |
                           (uint32_t)MEM_W(0, (int32_t)0x800CEBD4);
    const uint64_t adjusted = clock * 3 / 4;
    MEM_W(0, (int32_t)0x800CEBD0) = adjusted >> 32;
    MEM_W(0, (int32_t)0x800CEBD4) = adjusted;
    const uint32_t tv_type = MEM_W(0, (int32_t)0x80000300);
    MEM_W(0, (int32_t)0x800CEBD8) = tv_type == 0 ? 0x02F5B2D2 : tv_type == 2 ? 0x02E6025C : 0x02E6D354;
    std::fprintf(stderr, "SRW64_INIT mem=%u clock=%llu vi_clock=%u\n",
                 (uint32_t)MEM_W(0, (int32_t)0x80000318), (unsigned long long)adjusted,
                 (uint32_t)MEM_W(0, (int32_t)0x800CEBD8));
}

void on_vi() {
    const uint64_t vi = ++vi_count;
    if (vi % 6 == 0) {
        srw64::script_inject::poll_file(output_dir);
        std::ifstream command(output_dir / "control.txt");
        std::string magic, extra;
        uint64_t sequence{}, mask{}, duration{};
        if ((command >> magic >> sequence >> mask >> duration) && !(command >> extra) &&
            (magic == "SRWC1" || magic == "SRWQ1") && sequence > last_control_sequence && mask <= 0xFFFF && duration <= 600) {
            last_control_sequence = sequence;
            live_buttons = ((vi + duration) << 16) | mask;
            std::ofstream events(output_dir / "control-events.jsonl", std::ios::app);
            events << "{\"schema\":\"srw64.native-control-event.v1\",\"sequence\":" << sequence
                   << ",\"vi\":" << vi << ",\"mask\":" << mask << ",\"duration\":" << duration
                   << ",\"action\":\"" << (magic == "SRWQ1" ? "quit" : "buttons") << "\"}\n";
            if (magic == "SRWQ1") {
                control_quit = true;
                ultramodern::quit();
            }
        }
    }
    if (vi % 60 == 0) {
        const auto temporary = output_dir / "live-state.tmp";
        {
            std::ofstream state(temporary);
            state << "{\"schema\":\"srw64.native-live-state.v1\",\"vi\":" << vi
                  << ",\"max_vis\":" << max_vis << ",\"last_control_sequence\":" << last_control_sequence << "}\n";
        }
        std::filesystem::rename(temporary, output_dir / "live-state.json");
    }
    if (max_vis && vi >= max_vis) ultramodern::quit();
    // A mini-stage probe that named a command to watch ends once that command's
    // grace period is over, instead of running out the VI budget.
    if (srw64::mini_stage::finished()) ultramodern::quit();
}

bool get_input(int port, uint16_t* buttons, float* x, float* y) {
    if (port != 0) return false;
    const auto vi = vi_count.load();
    *buttons = vi < input_buttons.size() ? input_buttons[vi] : 0;
    const uint64_t live = live_buttons.load();
    if (vi < (live >> 16)) *buttons |= uint16_t(live);
    *x = *y = 0;
#if defined(SRW64_WITH_RT64)
    srw64_keyboard_input(buttons, x, y);
    if(srw64::battle_page::owns_input() || srw64::intermission_page::owns_input() || srw64::upgrade_page::owns_input() || srw64::parts_page::owns_input() || srw64::ability_page::owns_input() || srw64::names::owns_input() || srw64::link_page::owns_input() || srw64::settings_window::owns_input())*x=*y=0;
    *buttons = srw64::settings_window::filter_input(*buttons, *buttons != 0);
    *buttons = srw64::names::input(*buttons);
    *buttons = srw64::link_page::input(*buttons);
    *buttons = srw64::battle_page::input(*buttons);
    *buttons = srw64::intermission_page::input(*buttons);
    *buttons = srw64::upgrade_page::input(*buttons);
    *buttons = srw64::parts_page::input(*buttons);
    *buttons = srw64::ability_page::input(*buttons);
    *buttons = srw64::intro::input(*buttons);
    *buttons = srw64::mini_stage::input(*buttons);
    *buttons = srw64::dialogue::input(*buttons);
#endif
    return true;
}

ultramodern::input::connected_device_info_t get_device(int port) {
    using namespace ultramodern::input;
    return {port == 0 ? Device::Controller : Device::None, Pak::None};
}

RspUcodeFunc* get_microcode(const OSTask* task) {
    if (task->t.type != 2 || (uint32_t)task->t.ucode != 0x800C4910 ||
        (uint32_t)task->t.ucode_data != 0x800CF4D0) return nullptr;
    ++audio_tasks;
    return srw64_audio_probe;
}
}

uint64_t srw64_current_vi() { return vi_count.load(); }

// Debug interface hooks (debug_server.cpp): a button pulse that starts now
// rather than at the next control-file poll, and a quit reported like SRWQ1.
void srw64_debug_buttons(uint16_t mask, uint64_t duration_vis) {
    live_buttons = ((vi_count.load() + duration_vis) << 16) | mask;
}
void srw64_debug_quit() {
    control_quit = true;
    ultramodern::quit();
}

extern "C" void resident_func_8007F704(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t rom = ctx->r4, ram = ctx->r5, size = ctx->r6;
    if(srw64::upgrades::read_text(rom,rdram,ram,size)) {ctx->r2=0;return;}
#if defined(SRW64_WITH_RT64)
    if(srw64::names::read(rom,rdram,ram,size)) {ctx->r2=0;return;}
#endif
    srw64_original_rom_read(rdram, ctx);
    for (size_t index = 1; index < num_sections; ++index) {
        const auto& section = section_table[index];
        if (section.rom_addr != rom || section.size != size || (uint32_t)section.ram_addr != ram) continue;
        const auto rom_bytes = recomp::get_rom();
        if ((uint64_t)rom + size > rom_bytes.size()) fail("overlay outside ROM");
        for (size_t i = 0; i < size; ++i) {
            if ((uint8_t)MEM_B(i, (int32_t)ram) != rom_bytes[rom + i]) fail("overlay DMA byte mismatch");
        }
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
        srw64::upgrades::patch_copy(rdram, rom, ram, size);
#if defined(SRW64_WITH_RT64)
        srw64::intro::overlay_loaded(rom,ram,size);
        srw64::names::overlay_loaded(rom,ram,size);
        if(ram==0x801C2600)srw64::dialogue::overlay_loaded(rom);
#endif
        std::fprintf(stderr, "SRW64_OVERLAY rom=%08X ram=%08X size=%08X bytes=verified\n", rom, ram, size);
        return;
    }
    srw64::upgrades::patch_copy(rdram, rom, ram, size);
}

extern "C" void resident_func_8008C510(uint8_t* rdram, recomp_context* ctx) {
    uint32_t offset,size;
    if(srw64::upgrades::descriptor(rdram,uint16_t(ctx->r4),uint16_t(ctx->r5),offset,size)) {
        MEM_W(0,ctx->r6)=offset;MEM_W(0,ctx->r7)=size;ctx->r2=size;return;
    }
#if defined(SRW64_WITH_RT64)
    if(srw64::names::descriptor(uint16_t(ctx->r4),uint16_t(ctx->r5),offset,size)) {
        MEM_W(0,ctx->r6)=offset;MEM_W(0,ctx->r7)=size;ctx->r2=size;return;
    }
#endif
    srw64_original_text_descriptor(rdram,ctx);
}

static int run_host(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr, "Usage: srw64-host BASELINE_ROM NEW_OUTPUT_DIR MAX_VIS [INPUT_MASKS|-] [INITIAL_SRAM]\n");
        return 2;
    }
    output_dir = std::filesystem::absolute(argv[2]);
    const char* variant_key = std::getenv("SRW64_ROM_VARIANT");
    if (!variant_key) variant_key = "jp";
    const NativeRomVariant* variant = nullptr;
    for (const auto& known : native_rom_variants) {
        if (std::string(variant_key) == known.key) variant = &known;
    }
    if (!variant) fail("unknown ROM variant");
    if (std::filesystem::exists(output_dir)) fail("output directory already exists");
    std::filesystem::create_directories(output_dir);
    max_vis = std::stoull(argv[3]);
    const bool interactive = std::getenv("SRW64_INTERACTIVE") && std::string(std::getenv("SRW64_INTERACTIVE")) == "1";
    if ((!interactive && max_vis == 0) || max_vis > 216000) fail("VI limit outside diagnostic range");
    if (interactive && (max_vis != 0 || (argc >= 5 && std::string(argv[4]) != "-"))) fail("interactive mode requires unlimited VI and no scripted input");
    if (argc >= 5 && std::string(argv[4]) != "-") {
        std::ifstream input(argv[4], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.size() != 8 + (max_vis + 1) * 2 || std::string((char*)bytes.data(), 4) != "SRWI") fail("invalid input mask file");
        const uint32_t count = (uint32_t(bytes[4]) << 24) | (uint32_t(bytes[5]) << 16) | (uint32_t(bytes[6]) << 8) | bytes[7];
        if (count != max_vis + 1) fail("input mask count differs from VI limit");
        for (uint32_t i = 0; i < count; ++i) input_buttons.push_back((uint16_t(bytes[8 + i * 2]) << 8) | bytes[9 + i * 2]);
    }
    std::filesystem::create_directories(output_dir / "runtime-data");
    if (argc == 6) {
        if (std::filesystem::file_size(argv[5]) != 0x8000) fail("initial SRAM must be exactly 32 KiB");
        const auto saves = output_dir / "runtime-data/saves";
        std::filesystem::create_directories(saves);
        if (!std::filesystem::copy_file(argv[5], saves / variant->save_file)) fail("initial SRAM copy failed");
        std::fprintf(stderr, "SRW64_INITIAL_SRAM copied=32768\n");
    }
    recomp::register_config_path(output_dir / "runtime-data");
    recomp::overlays::register_overlays({section_table, num_sections, num_sections},
                                      {overlay_sections_by_index, std::size(overlay_sections_by_index)});
    recomp::GameEntry game{};
    game.rom_hash = variant->hash;
    game.internal_name = "\xBD\xB0\xCA\xDF\xB0\xDB\xCE\xDE\xAF\xC4\xC0\xB2\xBE\xDD" "64    ";
    game.display_name = "Super Robot Wars 64 native probe";
    game.game_id = variant->game_id;
    game.mod_game_id = "srw64";
    game.save_type = recomp::SaveType::Sram;
    game.is_enabled = true;
    game.entrypoint_address = (int32_t)0x80076610;
    game.entrypoint = recomp_entrypoint;
    game.on_init_callback = on_init;
    std::fprintf(stderr, "SRW64_ROM_VARIANT %s\n", variant->key);
    if (!recomp::register_game(game)) fail("game registration failed");
    if (recomp::select_rom(argv[1], game.game_id) != recomp::RomValidationError::Good) fail("baseline ROM validation failed");
    recomp::check_all_stored_roms();
    if (!recomp::is_rom_valid(game.game_id)) fail("stored ROM readback failed");
    char game_option[] = "--game", game_name[] = "srw64";
    char* runtime_args[] = {argv[0], game_option, game_name};
    recomp::Configuration cfg{};
    cfg.argc = 3;
    cfg.argv = runtime_args;
    cfg.project_version = {0, 0, 1, "cpu-probe"};
#if defined(SRW64_WITH_RT64)
    srw64_set_capture_directory(output_dir);
    srw64::debug::start(output_dir, {{"interactive", interactive}, {"max_vis", max_vis}, {"variant", variant->key}});
    srw64::dialogue::configure(output_dir);
    srw64::names::configure(output_dir);
    srw64::link_page::configure(output_dir);
    srw64::battle_page::configure(output_dir);
    srw64::intermission_page::configure(output_dir);
    srw64::upgrade_page::configure(output_dir);
    srw64::parts_page::configure(output_dir);
    srw64::ability_page::configure(output_dir);
    srw64::intro::configure(output_dir);
    srw64_configure_audio(std::getenv("SRW64_AUDIO_OUTPUT") && std::string(std::getenv("SRW64_AUDIO_OUTPUT")) == "1", output_dir);
    cfg.gfx_callbacks.create_window = srw64_create_window;
    cfg.gfx_callbacks.update_gfx = srw64_update_window;
    cfg.renderer_callbacks.create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle handle, bool)
        -> std::unique_ptr<ultramodern::renderer::RendererContext> {
        return std::make_unique<RecordingRenderer>(rdram, srw64_create_renderer(rdram, handle));
    };
#else
    cfg.gfx_callbacks.create_window = [](void*) { return ultramodern::renderer::WindowHandle{(void*)1, (void*)1}; };
    cfg.renderer_callbacks.create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle, bool)
        -> std::unique_ptr<ultramodern::renderer::RendererContext> { return std::make_unique<RecordingRenderer>(rdram); };
#endif
    cfg.rsp_callbacks.get_rsp_microcode = get_microcode;
    cfg.audio_callbacks.queue_samples = [](int16_t* samples, size_t count) {
        audio_samples += count;
#if defined(SRW64_WITH_RT64)
        srw64_queue_audio(samples, count);
#endif
    };
    cfg.audio_callbacks.get_frames_remaining = []() -> size_t {
#if defined(SRW64_WITH_RT64)
        return srw64_audio_remaining();
#else
        return 0;
#endif
    };
    cfg.audio_callbacks.set_frequency = [](uint32_t value) {
        frequency = value;
#if defined(SRW64_WITH_RT64)
        srw64_audio_frequency(value);
#endif
    };
    cfg.input_callbacks.get_input = get_input;
    cfg.input_callbacks.get_connected_device_info = get_device;
    cfg.events_callbacks.vi_callback = on_vi;
    cfg.error_handling_callbacks.message_box = [](const char* message) { std::fprintf(stderr, "SRW64_RUNTIME_ERROR %s\n", message); };
    recomp::start(cfg);
#if defined(SRW64_WITH_RT64)
    srw64_destroy_window();
#endif
    std::ofstream report(output_dir / "native-counters.json");
    report << "{\"schema\":\"srw64.native-cpu-probe-counters.v1\",\"vis\":" << vi_count
           << ",\"graphics_tasks\":" << dl_count << ",\"audio_tasks\":" << audio_tasks
           << ",\"audio_samples\":" << audio_samples << ",\"frequency\":" << frequency
           << ",\"control_quit\":" << (control_quit ? "true" : "false") << "}\n";
    return dl_count > 0 && audio_tasks > 0 ? 0 : 3;
}

// Keep the diagnostic positional ABI untouched for play_native.py and all probes.
int main(int argc, char** argv) {
#if defined(__APPLE__) && defined(SRW64_WITH_RT64)
    // Finder supplies no arguments. Explicit --play and the diagnostic ABI
    // below remain non-GUI and deterministic for the existing developer tools.
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--choose-rom")) {
        const auto ui = srw64::app::macos_desktop_ui();
        srw64::app::GameIdentity game{"srw64-jp-rev0", native_jp_sha256,
            native_rom_variants[0].save_file, srw64::rules::version, {}};
        for (const auto& rule : srw64::rules::catalog)
            game.rules.push_back({std::string(rule.id), rule.kind == srw64::rules::Kind::correction});
        return srw64::app::run_desktop({}, game.rom_sha256, ui,
            [&](const srw64::app::Options& options, const srw64::app::DesktopReady& ready) {
                return srw64::app::run_standalone(options, game, [&](int count, char** values) {
                    ready(); // The standalone Session owns the lock until run_host returns.
                    return run_host(count, values);
                });
            }, argc == 2 || srw64::app::macos_choose_another_rom());
    }
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::fputs(srw64::app::usage().c_str(), stdout);
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--play") {
#if defined(SRW64_WITH_RT64)
        try {
            if (argc == 3 && std::string_view(argv[2]) == "--help") {
                std::fputs(srw64::app::usage().c_str(), stdout);
                return 0;
            }
            std::vector<std::string_view> args;
            for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
            const auto options = srw64::app::parse_options(args);
            srw64::app::GameIdentity game{"srw64-jp-rev0", native_jp_sha256,
                native_rom_variants[0].save_file, srw64::rules::version, {}};
            for (const auto& rule : srw64::rules::catalog)
                game.rules.push_back({std::string(rule.id), rule.kind == srw64::rules::Kind::correction});
            return srw64::app::run_standalone(options, game, run_host);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s\n", error.what());
            return 2;
        }
#else
        std::fputs("--play requires the graphics host, not the headless probe.\n", stderr);
        return 2;
#endif
    }
    return run_host(argc, argv);
}
