#include "state_probe.hpp"
#include "app/runtime.hpp"
#include "console_log.hpp"
#include "hd_prepare.hpp"
#include "dialogue_data.hpp"
#include "crash_dump.hpp"
#include "bug_report.hpp"
// Native integration host, optionally rendered through RT64/Metal.
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <iostream>
#include <vector>
#ifdef __ANDROID__
#include <SDL_system.h>  // not SDL.h: its SDL_main.h renames main, which SDL looks up
#include <jni.h>
#endif

#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "funcs.h"
#include "recomp_overlays.inl"
#include "rom_variants.hpp"
#include "script_inject.hpp"
#include "mini_stage.hpp"
#include "rule_fixes.hpp"
#include "upgrade_refund.hpp"
#include "mod_data.hpp"
#include "mod_names.hpp"
#include "localization/catalog.hpp"
#include "parts_carry.hpp"
#include "graphics.hpp"
#include "run_files.hpp"
#include "app/console_data.hpp"
#include "app/launch.hpp"
#include "app/rom_import.hpp"
#include "app/desktop.hpp"
#include "app/rom_order.hpp"
#include "audio.hpp"
#include "native_dialogue.hpp"
#include "native_map.hpp"
#include "menu_widen.hpp"
#include "wide_map.hpp"
#include "battle_hud.hpp"
#include "game_frame.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <iterator>
#include "native_portrait.hpp"
#include "native_icon.hpp"
#include "native_background.hpp"
#include "native_sprite.hpp"
#include "sprite_text.hpp"
#include "ui_text.hpp"
#include "rom_art.hpp"
#include "game_hooks.hpp"
#include "native_intro.hpp"
#include "touch_scene.hpp"
#include "stage_info.hpp"
#include "game_speed.hpp"
#include "sched_rate.hpp"
#include "stall_watchdog.hpp"
#include "guest_memory.hpp"
#include "native_name_entry.hpp"
#include "unit_name.hpp"
#include "link_page.hpp"
#include "battle_page.hpp"
#include "intermission_page.hpp"
#include "upgrade_page.hpp"
#include "parts_page.hpp"
#include "ability_page.hpp"
#include "cheats.hpp"
#include "focus_lines.hpp"
#include "swap_page.hpp"
#include "save_page.hpp"
#include "save_store.hpp"
#include "autosave.hpp"
#include "quick_load.hpp"
#include "enemy_pilot_page.hpp"
#include "map_combo.hpp"
#include "legacy.hpp"
#include "music.hpp"
#include "title_page.hpp"
#include "battle_viewer.hpp"
#include "move_jump.hpp"
#include "enemy_cycle.hpp"
#include "settings_window.hpp"
#include "debug_server.hpp"
extern uint8_t* srw64_rdram;  // defined below, for debug_server.cpp

RspExitReason srw64_audio_probe(uint8_t*, uint32_t);

#ifdef __vita__
void srw64_vita_perf_thread(const char* label);  // vita/vita_platform.cpp
void srw64_vita_graphics_begin();                // graphics.cpp
#endif
namespace {
std::atomic<uint64_t> vi_count{}, dl_count{}, audio_tasks{}, audio_samples{};
std::atomic<uint32_t> frequency{};
std::filesystem::path output_dir;
std::optional<std::filesystem::path> initial_sram;  // the card the run started from
uint64_t max_vis = 600;
std::vector<size_t> loaded_sections;
std::vector<uint16_t> input_buttons;
std::atomic<uint64_t> live_buttons{}; // Upper bits: exclusive end VI; lower 16: N64 mask.
uint64_t last_control_sequence{};
std::atomic_bool control_quit{};
// An overlay is being copied in (resident_func_8007F704). The original waits on each 0x400-byte
// DMA, so the frame boundary (80085F30) can run in the middle; the frame hooks that patch an
// overlay's own bytes (battle_hud's bands, menu_widen's clamp) wait for the copy to end, or
// they patch the half-loaded overlay and the byte check below stops the game. On Android the
// copy spans frames (the Seeker, 2026-10-17: 3 of 29 battles ended by Rewind stopped here).
std::atomic_bool overlay_loading{};

[[noreturn]] void fail(const char* text) {
    std::fprintf(stderr, "SRW64_HOST_FAILURE %s\n", text);
    std::abort();
}

class RecordingRenderer final : public ultramodern::renderer::RendererContext {
public:
    explicit RecordingRenderer(uint8_t*, std::unique_ptr<ultramodern::renderer::RendererContext> renderer = {})
        : renderer(std::move(renderer)) {
        setup_result = this->renderer ? this->renderer->get_setup_result() : ultramodern::renderer::SetupResult::Success;
        chosen_api = this->renderer ? this->renderer->get_chosen_api() : ultramodern::renderer::GraphicsApi::Auto;
    }
    bool valid() override { return !renderer || renderer->valid(); }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        if (renderer) renderer->send_dl(task);
        srw64::stall::frame_done();
        const auto count = ++dl_count;
        if (count <= 3 || count % 60 == 0) {
            std::fprintf(stderr, "SRW64_GFX task=%llu ucode=%08X commands=%08X size=%u\n",
                         (unsigned long long)count, (uint32_t)task->t.ucode,
                         (uint32_t)task->t.data_ptr, task->t.data_size);
        }
    }
    void send_dummy_workload(uint32_t address) override { if (renderer) renderer->send_dummy_workload(address); }
    void update_screen() override { if (renderer) renderer->update_screen(); }
    void shutdown() override { if (renderer) renderer->shutdown(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
private:
    std::unique_ptr<ultramodern::renderer::RendererContext> renderer;
};

void on_init(uint8_t* rdram, recomp_context*) {
    srw64::app::boot_step("game init");
    srw64_rdram = rdram;
    srw64::stall::start();
    srw64::state_probe::directory=output_dir;
    srw64::mini_stage::configure(output_dir);
    srw64::rules::configure(output_dir);
    const auto rom=recomp::get_rom();
    srw64::upgrades::initialize(rom.data(),rom.size());
    srw64::upgrades::configure(output_dir);
    srw64::mod_data::configure({rom.data(),rom.size()},output_dir);
    srw64::refund::configure(output_dir);
    srw64::parts_carry::configure(output_dir);
    srw64::names::initialize_rom(rom.data(),rom.size());
    srw64::rom_art::initialize({rom.data(),rom.size()});
    // With an HD art pack, BANPRESTO and GAME OVER are upscaled from the ROM here rather
    // than shipped (docs/native/native-title-and-story-images.md).
    if(const char* art=std::getenv("SRW64_ART_PACK");art && *art)
        for(const auto& spec:srw64::rom_art::flat_scenes())
            srw64::sprites::add_generated_image(spec.scene,spec.atlas,spec.palette,{0},[spec] {
                srw64::sprites::TextImage out;
                auto image=srw64::rom_art::flat_image(spec);
                out.width=uint32_t(image.width);out.height=uint32_t(image.height);
                for(size_t i=0;i<image.rgba.size();i+=4)
                    for(int c=0;c<3;++c)image.rgba[i+c]=uint8_t((image.rgba[i+c]*image.rgba[i+3]+127)/255);
                out.rgba=std::move(image.rgba);
                return out;
            });
    // The generic runtime initially loads/registers 1 MiB. Only the resident
    // section belongs at this entrypoint; the game's own loader owns overlays.
    unload_overlays((int32_t)0x80076610, 0x100000);
    load_overlays(0x1000, (int32_t)0x80076610, 0x5AC30);
    srw64::app::boot_step("game entry");
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
#ifdef __vita__
    srw64_vita_perf_thread("vi");
#endif
    // At 120 Hz the host's VI count keeps 60 Hz units (sched_rate.hpp).
    static uint64_t ticks = 0;
    if (++ticks % srw64::sched_rate::factor()) return;
    const uint64_t vi = ++vi_count;
#if defined(__vita__) || defined(__SWITCH__)
    // The PS Vita and the Switch are driven through the debug interface only: no control.txt
    // to read (its absence, ten times a second, would be ten lookups of a missing file on a
    // memory card) or live state.
    const bool run_files = false;
#else
    const bool run_files = srw64_run_files();
#endif
    if (run_files && vi % 6 == 0) {
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
    if (run_files && vi % 60 == 0) {
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
    srw64_keyboard_input(buttons, x, y);
    if(srw64::battle_page::owns_input() || srw64::intermission_page::owns_input() || srw64::upgrade_page::owns_input() || srw64::parts_page::owns_input() || srw64::ability_page::owns_input() || srw64::swap_page::owns_input() || srw64::save_page::owns_input() || srw64::title_page::owns_input() || srw64::names::owns_input() || srw64::link_page::owns_input() || srw64::settings_window::owns_input())*x=*y=0;
    *buttons = srw64::settings_window::filter_input(*buttons, *buttons != 0);
    *buttons = srw64::names::input(*buttons);
    *buttons = srw64::link_page::input(*buttons);
    *buttons = srw64::battle_page::input(*buttons);
    *buttons = srw64::intermission_page::input(*buttons);
    *buttons = srw64::upgrade_page::input(*buttons);
    *buttons = srw64::parts_page::input(*buttons);
    *buttons = srw64::ability_page::input(*buttons);
    *buttons = srw64::swap_page::input(*buttons);
    *buttons = srw64::save_page::input(*buttons);
    *buttons = srw64::title_page::input(*buttons);
    *buttons = srw64::intro::input(*buttons);
    *buttons = srw64::mini_stage::input(*buttons);
    *buttons = srw64::dialogue::input(*buttons);
    // A running quick load presses Z + START itself, last: the settings window's release
    // gate above would hold its buttons back as keys never let go (quick_load.hpp).
    *buttons = uint16_t(srw64::quick_load::filter_input(*buttons));
    // The scene for the phone's touch controls (touch_scene.hpp).
    if (srw64_rdram) {
        using namespace srw64::touch_scene;
        current() = uint8_t(decide(srw64::guest::read(srw64_rdram, mode_address, 1), srw64::guest::read(srw64_rdram, map_state_address, 1),
                                   srw64::guest::read(srw64_rdram, map_sub_address, 1), srw64::intro::title_major(), srw64::dialogue::reading(),
                                   srw64::intro::title_waiting()));
        story_running() = story(srw64::guest::read(srw64_rdram, mode_address, 1),
                                srw64::guest::read(srw64_rdram, script_status_address, 2));
        // The stage info page's snapshot while it is up (stage_info.hpp).
        srw64::stage_info::sample(srw64_rdram);
    }
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
#ifdef __vita__
    srw64_vita_perf_thread("rsp");
#endif
    return srw64_audio_probe;
}
}

uint64_t srw64_current_vi() { return vi_count.load(); }

#ifdef __SWITCH__
// The Switch's stall report (stall_watchdog.cpp, as the PS Vita's watchdog in psv-port.md):
// each N64 thread's state, from the threads ultramodern names as they start: in the run
// queue, waiting on a message queue (with that queue's fill and its first message), or
// neither (the one running, a stopped one, or one held in host code). The name keeps its
// "Game" prefix, which places the thread on its core (switch/switch_platform.cpp).
namespace game_threads {
std::mutex threads_mutex;
std::vector<const OSThread*> threads;
std::string name_thread(const OSThread* t) {
    {
        std::lock_guard lock(threads_mutex);
        if (std::find(threads.begin(), threads.end(), t) == threads.end()) threads.push_back(t);
    }
    return "Game Thread " + std::to_string(t->id);
}
}
extern "C" void srw64_game_threads_report() {
    std::fprintf(stderr, "SRW64_STALL_COUNTS lists=%llu vi=%llu audio=%llu\n", (unsigned long long)dl_count.load(),
                 (unsigned long long)vi_count.load(), (unsigned long long)audio_tasks.load());
    std::lock_guard lock(game_threads::threads_mutex);
    for (const OSThread* t : game_threads::threads) {
        const uint32_t queue = uint32_t(t->queue);
        std::fprintf(stderr, "SRW64_STALL_THREAD id=%d priority=%d state=%d at=%08X", int(t->id), int(t->priority), int(t->state),
                     uint32_t(reinterpret_cast<const uint8_t*>(t) - srw64_rdram) | 0x80000000u);
        if (t->queue == ultramodern::running_queue) std::fprintf(stderr, " runnable");
        else if (!queue) std::fprintf(stderr, " in-no-queue");
        else {
            // blocked_on_recv is the queue's first member, blocked_on_send its second.
            const bool send = (queue & 7) == 4;
            const uint32_t mq_address = send ? queue - 4 : queue;
            const auto* mq = reinterpret_cast<const OSMesgQueue*>(srw64_rdram + (mq_address & 0x3FFFFFF));
            std::fprintf(stderr, " waits-to-%s mq=%08X messages=%d/%d", send ? "send" : "receive", mq_address,
                         int(mq->validCount), int(mq->msgCount));
        }
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}
#endif
// Guest RDRAM, for the debug interface's read-only memory view (debug_server.cpp).
uint8_t* srw64_rdram = nullptr;
// The runtime's VI thread asks before each picture latches (sched_rate.hpp latch()).
extern "C" bool srw64_vi_latch(uint64_t tick) { return srw64::sched_rate::latch(srw64_rdram, tick); }
// The swap after a display list left undrawn latches at once (draw_skip.hpp, runtime events.cpp).
extern "C" bool srw64_vi_swap_now() { return srw64::draw_skip::swap_now.exchange(false); }

// Debug interface hooks (debug_server.cpp): a button pulse that starts now
// rather than at the next control-file poll, and a quit reported like SRWQ1.
void srw64_debug_buttons(uint16_t mask, uint64_t duration_vis) {
    live_buttons = ((vi_count.load() + duration_vis) << 16) | mask;
}
void srw64_debug_quit() {
    control_quit = true;
    ultramodern::quit();
}

// The overlay's bytes against the ROM's: how many differ. (A 64-bit index: MEM_B needs the
// address sign-extended.)
static size_t overlay_mismatch(uint8_t* rdram, uint32_t ram, const uint8_t* expected, uint32_t size) {
    size_t differing = 0;
    for (size_t i = 0; i < size; ++i) differing += (uint8_t)MEM_B(i, (int32_t)ram) != expected[i];
    return differing;
}
// Where an overlay differs from the ROM (runs of differing bytes, gaps under 16 joined),
// what was written there, and whether it is still being written 100 ms later.
static void report_overlay_mismatch(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size, const uint8_t* expected) {
    std::vector<uint8_t> now(size);
    for (size_t i = 0; i < size; ++i) now[i] = (uint8_t)MEM_B(i, (int32_t)ram);
    std::vector<std::pair<uint32_t, uint32_t>> runs;
    size_t differing = 0;
    for (size_t i = 0; i < size; ++i) {
        if (now[i] == expected[i]) continue;
        ++differing;
        if (!runs.empty() && i - runs.back().second < 16) runs.back().second = uint32_t(i + 1);
        else runs.push_back({uint32_t(i), uint32_t(i + 1)});
    }
    std::fprintf(stderr, "SRW64_OVERLAY_MISMATCH rom=%08X ram=%08X size=%08X bytes=%zu runs=%zu vi=%llu mode=%X map_state=%X\n",
                 rom, ram, size, differing, runs.size(), (unsigned long long)vi_count.load(),
                 srw64::guest::read(rdram, 0x8015DA02, 1), srw64::guest::read(rdram, 0x80172EB0, 1));
    for (size_t r = 0; r < runs.size() && r < 12; ++r) {
        const auto [start, end] = runs[r];
        char seen[97] = {}, wanted[97] = {};
        for (uint32_t i = 0; i < 48 && start + i < end; ++i) {
            std::snprintf(seen + 2 * i, 3, "%02X", now[start + i]);
            std::snprintf(wanted + 2 * i, 3, "%02X", expected[start + i]);
        }
        std::fprintf(stderr, "SRW64_OVERLAY_MISMATCH run %08X-%08X (+%X, %X bytes) ram %s rom %s\n",
                     ram + start, ram + end, start, end - start, seen, wanted);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    size_t later = 0;
    uint32_t later_first = 0;
    for (size_t i = 0; i < size; ++i)
        if ((uint8_t)MEM_B(i, (int32_t)ram) != now[i]) { if (!later) later_first = uint32_t(i); ++later; }
    std::fprintf(stderr, "SRW64_OVERLAY_MISMATCH 100 ms later: %zu more bytes changed%s", later, later ? "" : "\n");
    if (later) std::fprintf(stderr, ", first at %08X\n", ram + later_first);
}

extern "C" void resident_func_8007F704(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t rom = ctx->r4, ram = ctx->r5, size = ctx->r6;
    if(srw64::upgrades::read_text(rom,rdram,ram,size)) {ctx->r2=0;return;}
    srw64::mod_data::refresh();   // number MODs join the copy patches (mod_data.hpp)
    overlay_loading = true;
    srw64_original_rom_read(rdram, ctx);
    overlay_loading = false;
    for (size_t index = 1; index < num_sections; ++index) {
        const auto& section = section_table[index];
        if (section.rom_addr != rom || section.size != size || (uint32_t)section.ram_addr != ram) continue;
        const auto rom_bytes = recomp::get_rom();
        if ((uint64_t)rom + size > rom_bytes.size()) fail("overlay outside ROM");
        if (const auto differing = overlay_mismatch(rdram, ram, rom_bytes.data() + rom, size); differing) {
            // Something wrote the overlay while the original copied it in (it waits on each
            // 0x400-byte DMA, so other threads run): say what and where, then stop, or put the
            // ROM's bytes back and go on with SRW64_OVERLAY_REPAIR=1.
            report_overlay_mismatch(rdram, rom, ram, size, rom_bytes.data() + rom);
            const char* repair = std::getenv("SRW64_OVERLAY_REPAIR");
            if (!repair || std::string(repair) != "1") fail("overlay DMA byte mismatch");
            for (size_t i = 0; i < size; ++i) MEM_B(i, (int32_t)ram) = int8_t(rom_bytes[rom + i]);
            std::fprintf(stderr, "SRW64_OVERLAY_MISMATCH repaired rom=%08X\n", rom);
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
        srw64::intro::overlay_loaded(rom,ram,size);
        srw64::title_page::overlay_loaded(rom,ram,size);
        srw64::names::overlay_loaded(rom,ram,size);
        if(ram==0x801C2600)srw64::dialogue::overlay_loaded(rom);
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
    srw64_original_text_descriptor(rdram,ctx);
}

static int run_host(int argc, char** argv) {
    if (argc < 4 || argc > 6) {
        std::fprintf(stderr, "Usage: srw64-gfx-host BASELINE_ROM NEW_OUTPUT_DIR MAX_VIS [INPUT_MASKS|-] [INITIAL_SRAM]\n");
        return 2;
    }
    // The dialogue data, and on the PS Vita its graphics, start on threads of their own:
    // they overlap the ROM's loading and each other.
    srw64::dialogue_data::preload();
#ifdef __vita__
    srw64_vita_graphics_begin();
#endif
    output_dir = srw64::app::absolute_path(argv[2]);
    const char* variant_key = std::getenv("SRW64_ROM_VARIANT");
    if (!variant_key) variant_key = "jp";
    const NativeRomVariant* variant = nullptr;
    for (const auto& known : native_rom_variants) {
        if (std::string(variant_key) == known.key) variant = &known;
    }
    if (!variant) fail("unknown ROM variant");
#if !defined(__vita__) && !defined(__SWITCH__)
    if (std::filesystem::exists(output_dir)) fail("output directory already exists");
#endif
    // The PS Vita and the Switch run in the same folder each time, prepared with their data
    // (run_prepared).
    std::filesystem::create_directories(output_dir);
    srw64::console_log::attach(output_dir);
#ifdef _WIN32
    srw64::crash_dump::attach(output_dir);
#endif
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
        std::error_code copied;
        if (!srw64::app::copy_file(argv[5], saves / variant->save_file, false, copied)) fail("initial SRAM copy failed");
        std::fprintf(stderr, "SRW64_INITIAL_SRAM copied=32768\n");
        initial_sram = saves / variant->save_file;
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
    srw64_set_capture_directory(output_dir);
    srw64::debug::start(output_dir, {{"interactive", interactive}, {"max_vis", max_vis}, {"variant", variant->key}});
    srw64::app::boot_step("dialogue load");
    srw64::dialogue::configure(output_dir);
    srw64::app::boot_step("host setup");
    // The tactical map wider than the original (docs/design/deck-16x10.md §5). Sprites that
    // scroll with the map (sub-record +2 == 1: the map, units and cursor, map effects) and
    // the map overlay's own callbacks go in the widened view; the rest (windows, banners,
    // portraits) stays in the centred 320. Not in the whole-map overview (801027DB == 1).
    srw64_game_hooks.frame_start = [](uint8_t* ram) {
        srw64::wide_map::frame(ram);
        if (!overlay_loading) { srw64::battle_hud::frame(ram); srw64::menu_widen::frame_start(ram); }
        srw64::game_speed::frame(ram);   // 2x speed where it applies (game_speed.hpp)
        srw64::quick_load::frame(ram);   // a confirmed load of the suspend save (quick_load.hpp)
        srw64::battle_page::frame(ram);  // a battle page the game left by a load (battle_page.hpp)
        srw64::enemy_pilot_page::mark_fought(ram);   // every enemy counts as fought (enemy_pilot_page.hpp)
        srw64::map_combo::frame(ram);    // the weapon page's combination lines go with the map (map_combo.hpp)
    };
    srw64_game_hooks.map_space_begin = [](uint8_t* ram, int32_t cursor, uint32_t function, uint32_t slot, uint32_t sub) {
        if (ram[0x001027DB ^ 3] == 1) return false;
        // The map overlay's callbacks, whichever call reaches them (a node registered by
        // 8008A5C4 may carry a slot too): grid, move range, attack ranges, target tiles,
        // damage figures (80209900) and 3D56's target area (800A3AD8). Other overlays use
        // the same addresses, so only while the widened map is on screen.
        static constexpr uint32_t map_callbacks[] = {0x801E44BC, 0x801E4760, 0x801E4C44, 0x801E4C88,
            0x801E4CCC, 0x801E4D10, 0x80209900, 0x800A3AD8};
        if (std::find(std::begin(map_callbacks), std::end(map_callbacks), function) != std::end(map_callbacks)) {
            if (!srw64::wide_map::map_shown()) return false;
            // 3D44's choice (8009FA94) registers the same callback on the same record (0x23) for
            // its cursor bar, in the window's own coordinates: it stays centred with the window
            // (a player, 2026-10-12: the green bar stuck out left of the choice).
            if (function == 0x800A3AD8 && srw64::guest::read(ram, 0x8015F950 + 0x948 + 0x30, 4) == 0x8009FA94) return false;
        } else {
            // Sprites that scroll with the map (sub-record +2 == 1): the map itself (mode 8,
            // 800945D4), units and the cursor (modes 5 and 6), map effects (modes 12 and 13).
            // The intermission picture (mode 4) also carries the flag but is no map.
            static constexpr uint32_t map_sprites[] = {0x800945D4, 0x801E2C5C, 0x801E26C0, 0x800975A4, 0x8009751C};
            if (slot >= 300 || sub >= 4 || ram[(0x000FFAAE + slot * 0xC4 + sub * 0x30) ^ 3] != 1 ||
                std::find(std::begin(map_sprites), std::end(map_sprites), function) == std::end(map_sprites))
                return false;
            // Only the map opens the view; the rest follow it while it is on screen.
            if (function != 0x800945D4 && !srw64::wide_map::map_shown()) return false;
        }
        return srw64::wide_map::begin(ram, cursor);
    };
    srw64_game_hooks.map_space_end = [](uint8_t* ram, int32_t cursor) { srw64::wide_map::end(ram, cursor); };
    srw64_game_hooks.map_stretch = [](uint8_t* ram, uint32_t slot, uint32_t sub) {
        if (!srw64::frame::wide || srw64::frame::picture_width <= srw64::frame::kWidth || slot >= 300 || sub >= 4) return false;
        // The focus lines (layouts 610-612) radiate from the centre and end at the 4:3
        // edges; stretched, they reach the picture's (docs/design/deck-16x10.md).
        uint16_t layout;
        std::memcpy(&layout, ram + ((0x000FFA70 + slot * 0xC4 + 0x3C + sub * 0x30 + 4) ^ 2), 2);
        return layout >= 610 && layout <= 612;
    };
    srw64_game_hooks.map_underlay = [](uint8_t* ram, int32_t cursor, uint32_t slot, uint32_t sub) {
        // The focus lines behind a unit's picture on a native page: its panel under them.
        if (srw64::focus_lines::is_lines(ram, slot, sub) &&
            (srw64::ability_page::owns_input() || srw64::upgrade_page::owns_input() || srw64::swap_page::owns_input()))
            srw64::focus_lines::panel(ram, cursor);
    };
    srw64_game_hooks.map_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end, uint32_t slot, uint32_t sub) {
        // Sprite record read by 800945D4: base 800FFA70 + slot*0xC4, sub-record at +0x3C + sub*0x30.
        const uint32_t base = 0x000FFA70 + slot * 0xC4, record = base + 0x3C + sub * 0x30;
        auto byte = [&](uint32_t a) { return ram[a ^ 3]; };
        auto half = [&](uint32_t a) { uint16_t v; std::memcpy(&v, ram + (a ^ 2), 2); return v; };
        auto word = [&](uint32_t a) { uint32_t v; std::memcpy(&v, ram + a, 4); return v; };
        const bool overview = byte(record + 3) != 0;  // 800943E0 scaling: the whole map on one screen
        float origin_x, origin_y;
        const uint32_t ox = word(base + 4), oy = word(base + 8);
        std::memcpy(&origin_x, &ox, 4); std::memcpy(&origin_y, &oy, 4);
        int32_t offset_x = int32_t(origin_x), offset_y = int32_t(origin_y);
        if (byte(record + 2) == 1) { offset_x += int32_t(word(0x0010F5D4)); offset_y += int32_t(word(0x0010F5D8)); }
        // The widened menus' frames (menu_widen.hpp), before anything reads their rectangles.
        if (!overview) srw64::menu_widen::frame_drawn(ram, begin, end, half(record + 4), offset_x, offset_y);
        // Mode 9 scenes the text describer knows (battle HUD banners and badges).
        srw64::sprites::rewrite_grid(ram, {begin, end, slot, sub, false});
        // Screen = map + offset, so the map pixel at screen (0,0) is -offset.
        srw64::hdmap::rewrite(ram, {begin, end, half(record + 4), -offset_x, -offset_y, overview, srw64::wide_map::view_offset()});
    };
    srw64_game_hooks.terrain_panel_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end) {
        srw64::hdmap::rewrite_panel(ram, {begin, end});
    };
    srw64::dialogue::battle_quote_opened = srw64::music::battle_quote;   // MOD battle voices
    srw64_game_hooks.unit_icon_drawn = [](uint8_t* ram, uint32_t slot, uint32_t begin, uint32_t end) {
        srw64::unit_icons::rewrite(ram, slot, begin, end);
    };
    srw64_game_hooks.unit_shadow_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end) {
        srw64::unit_icons::rewrite_shadow(ram, begin, end);
    };
    srw64_game_hooks.portrait_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end, uint32_t, uint32_t) {
        // The sprite record only holds resource handles; the portrait is recognised from
        // the texture data the drawn display list points at.
        srw64::portraits::rewrite(ram, {begin, end});
    };
    // The battle's sky (mode 2 while the battle animation runs) is drawn a period to each
    // side in a picture wider than 4:3.
    srw64_game_hooks.background_sides = [](uint8_t*, uint32_t, uint32_t) {
        return srw64::frame::wide && srw64::frame::picture_width > srw64::frame::kWidth && srw64::wide_map::battle_shown();
    };
    // Without the bands (battle_hud.cpp, any automatic aspect) its rows that wrap from one
    // edge to the other show: the slot's y (800FFA70 + slot * 0xC4 + 8, a float). Layers of
    // kinds 2, 3 and 9 (+1, the table at 800C5960) take it from the camera's pitch in
    // 80084D90, -(pitch - 4.8) * 3.5: 0 at rest, +16.8 in the close-up (pitch 0), down to
    // -53.2 while the camera slides in or looks askance (pitch 20). Layers that drift on
    // their own (kinds 4-7) wrap by design and keep to the original.
    srw64_game_hooks.background_wrap = [](uint8_t* ram, uint32_t slot, uint32_t) {
        if (!srw64::frame::wide || !srw64::wide_map::battle_shown() || slot > 4) return 0.f;   // the sky layers, slots 0-4
        const uint32_t kind = srw64::guest::read(ram, 0x800FFA71 + slot * 0xC4, 1);
        if (kind != 2 && kind != 3 && kind != 9) return 0.f;
        float y;
        const uint32_t bits = srw64::guest::read(ram, 0x800FFA70 + slot * 0xC4 + 8, 4);
        std::memcpy(&y, &bits, 4);
        return std::isfinite(y) && std::abs(y) >= .25f && std::abs(y) <= 64.f ? y : 0.f;
    };
    srw64_game_hooks.background_whole = [](uint8_t* ram, uint32_t slot, uint32_t sub) {
        return srw64::backgrounds::battle_sky(ram, slot, sub);
    };
    srw64_game_hooks.background_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end, uint32_t slot, uint32_t sub, bool sides) {
        // A background still 4:3 (the intermission pictures without HD art) leaves the
        // sides black (docs/design/deck-16x10.md).
        if (!srw64::backgrounds::rewrite(ram, {begin, end, slot, sub}) && !sides) srw64::wide_map::mark_original();
    };
    srw64_game_hooks.scene_drawn = [](uint8_t* ram, uint32_t begin, uint32_t end, uint32_t slot, uint32_t sub, bool quads) {
        srw64::sprites::rewrite(ram, {begin, end, slot, sub, quads});
    };
    srw64_game_hooks.scene_sides = [](uint8_t* ram, uint32_t begin, uint32_t end, uint32_t slot, uint32_t sub) {
        return srw64::frame::wide && float(srw64::frame::picture_width) > srw64::frame::kWidth + 0.5f &&
               srw64::sprites::repeats_across(ram, {begin, end, slot, sub, false});
    };
    srw64::sprites::set_text(srw64::sprite_text::describe);
    srw64::app::boot_step("text pages");
    srw64::ui_text::configure(output_dir);
    srw64::names::configure(output_dir);
    srw64::unit_name::configure(output_dir);
    {
        // Names from MOD packages over the defaults just registered (mod_names.hpp).
        std::vector<std::string> locales;
        for(const auto& [locale,catalog]:srw64::localization::registered())if(catalog)locales.push_back(locale);
        srw64::mod_names::configure(std::move(locales),output_dir);
        srw64::names::set_default_names_refresh(&srw64::mod_names::refresh);
        // MOD portraits go by pilot number: each pilot's original (image, palette).
        std::map<unsigned, srw64::portraits::PilotImage> images;
        if (const auto* data = srw64::dialogue_data::document(); data && data->contains("battle_assets") && data->at("battle_assets").contains("portraits"))
            for (const auto& [pilot, row] : data->at("battle_assets").at("portraits").items()) {
                const auto& r = row.at("resources");
                if (r.size() == 2) images[unsigned(std::stoul(pilot))] = {r[0].get<uint16_t>(), r[1].get<uint16_t>(), row.value("path", std::string())};
            }
        srw64::portraits::set_pilot_images(std::move(images));
    }
    srw64::link_page::configure(output_dir);
    srw64::battle_page::configure(output_dir);
    srw64::intermission_page::configure(output_dir);
    srw64::upgrade_page::configure(output_dir);
    srw64::parts_page::configure(output_dir);
    srw64::ability_page::configure(output_dir);
    srw64::swap_page::configure(output_dir);
    srw64::cheats::configure(output_dir);
    srw64::save_store::configure(output_dir, initial_sram);
    if (srw64::save_store::enabled()) srw64_game_hooks.sram_transfer = srw64::save_store::transfer;
    srw64::autosave::configure(output_dir);
    srw64::quick_load::configure(output_dir);
    srw64::legacy::configure(output_dir);
    srw64::music::configure();
    srw64::save_page::configure(output_dir);
    srw64::title_page::configure(output_dir);
    srw64::battle_viewer::install(output_dir);
    srw64::move_jump::configure(output_dir);
    srw64::enemy_cycle::configure(output_dir);
    srw64::intro::configure(output_dir);
    srw64_configure_audio(std::getenv("SRW64_AUDIO_OUTPUT") && std::string(std::getenv("SRW64_AUDIO_OUTPUT")) == "1", output_dir);
    cfg.gfx_callbacks.create_window = srw64_create_window;
    cfg.gfx_callbacks.update_gfx = srw64_update_window;
    cfg.renderer_callbacks.create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle handle, bool)
        -> std::unique_ptr<ultramodern::renderer::RendererContext> {
        return std::make_unique<RecordingRenderer>(rdram, srw64_create_renderer(rdram, handle));
    };
    cfg.rsp_callbacks.get_rsp_microcode = get_microcode;
    cfg.audio_callbacks.queue_samples = [](int16_t* samples, size_t count) {
        audio_samples += count;
        srw64_queue_audio(samples, count);
    };
    cfg.audio_callbacks.get_frames_remaining = []() -> size_t {
        return srw64_audio_remaining();
    };
    cfg.audio_callbacks.set_frequency = [](uint32_t value) {
        frequency = value;
        srw64_audio_frequency(value);
    };
    cfg.input_callbacks.get_input = get_input;
    cfg.input_callbacks.get_connected_device_info = get_device;
    cfg.events_callbacks.vi_callback = on_vi;
#ifdef __vita__
    cfg.threads_callbacks.get_game_thread_name = vita_watchdog::name_thread;
    vita_watchdog::start();
#endif
#ifdef __SWITCH__
    cfg.threads_callbacks.get_game_thread_name = game_threads::name_thread;
#endif
    cfg.error_handling_callbacks.message_box = [](const char* message) { std::fprintf(stderr, "SRW64_RUNTIME_ERROR %s\n", message); };
    // Every module has taken what it keeps from the dialogue data.
    srw64::dialogue_data::release();
    srw64::app::boot_step("game start");
    recomp::start(cfg);
    srw64_destroy_window();
    std::ofstream report(output_dir / "native-counters.json");
    report << "{\"schema\":\"srw64.native-cpu-probe-counters.v1\",\"vis\":" << vi_count
           << ",\"graphics_tasks\":" << dl_count << ",\"audio_tasks\":" << audio_tasks
           << ",\"audio_samples\":" << audio_samples << ",\"frequency\":" << frequency
           << ",\"control_quit\":" << (control_quit ? "true" : "false") << "}\n";
    return dl_count > 0 && audio_tasks > 0 ? 0 : 3;
}

#ifdef __ANDROID__
namespace {
// The variables SRW64Activity set (its resource folder, HOME, the debug switch...) into
// this C library too: the arm64 game run by ARM translation on an x86 device or emulator
// has its own, which never sees Java's Os.setenv (HostEnvironment.java).
void import_java_environment() {
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    const jclass type = env->GetObjectClass(activity);
    const jmethodID method = env->GetMethodID(type, "hostEnvironment", "()[Ljava/lang/String;");
    auto pairs = method ? static_cast<jobjectArray>(env->CallObjectMethod(activity, method)) : nullptr;
    if (env->ExceptionCheck()) { env->ExceptionClear(); pairs = nullptr; }
    for (jsize i = 0, count = pairs ? env->GetArrayLength(pairs) : 0; i < count; ++i) {
        auto pair = static_cast<jstring>(env->GetObjectArrayElement(pairs, i));
        if (const char* text = pair ? env->GetStringUTFChars(pair, nullptr) : nullptr) {
            const std::string_view value(text);
            if (const auto equals = value.find('='); equals != std::string_view::npos && equals > 0)
                setenv(std::string(value.substr(0, equals)).c_str(), text + equals + 1, 1);
            env->ReleaseStringUTFChars(pair, text);
        }
        if (pair) env->DeleteLocalRef(pair);
    }
    if (pairs) env->DeleteLocalRef(pairs);
    env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
// A crash leaves its stack on stderr and in the run's console.log, for reports from
// Linux and the Steam Deck: addresses resolve with addr2line on the unstripped build.
#include <execinfo.h>
#include <csignal>
#include <unistd.h>
namespace {
void crash_backtrace(int signal) {
    static const char header[] = "SRW64_CRASH signal ";
    char number[4] = {char('0' + signal / 10 % 10), char('0' + signal % 10), '\n', 0};
    void* frames[64];
    const int count = backtrace(frames, 64);
    // Past the pipes (console_log.hpp), which may not be read again: what they still hold
    // first, then the stack to the original stderr and console.log.
    srw64::console_log::flush_now();
    for (const int fd : {srw64::console_log::original_stderr(), srw64::console_log::crash_fd()}) {
        if (fd < 0) continue;
        (void)!::write(fd, header, sizeof(header) - 1);
        (void)!::write(fd, number, 3);
        backtrace_symbols_fd(frames, count, fd);
    }
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
}
#endif


#ifdef __SWITCH__
bool srw64_switch_application();                                                   // switch/switch_platform.cpp
void srw64_switch_boot_line(const char* line);
void srw64_switch_error(const std::string& message, const std::string& details);
std::string srw64_switch_language();
#endif

// Keep the diagnostic positional ABI untouched for play_native.py and all probes.
int main(int argc, char** argv) {
#ifdef __ANDROID__
    import_java_environment();
#endif
#if defined(_WIN32)
    // A double-click's own console goes first, before any handle exists for FreeConsole
    // to close by mistake (desktop_windows.cpp).
    const bool desktop_start = argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--choose-rom");
    if (desktop_start) srw64::app::windows_release_console();
#endif
    // stdout and stderr also into the run's console.log (Android: to logcat, which would
    // otherwise drop them), for a bug report (console_log.hpp).
    srw64::console_log::start();
#ifdef _WIN32
    // A crash's minidump into the run directory, for the same report (crash_dump.hpp).
    srw64::crash_dump::install();
#endif
    // Which release, on what: the first lines of every console.log (bug_report.hpp).
    srw64::bug_report::log_start();
#if defined(__linux__) && !defined(__ANDROID__)
    // Android: bionic has backtrace() only from API 33, and debuggerd's tombstone needs
    // its own handlers, so crashes go to logcat as tombstones.
    for (int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) std::signal(signal, crash_backtrace);
#endif
#if defined(__APPLE__)
    // Finder supplies no arguments. Explicit --play and the diagnostic ABI
    // below remain non-GUI and deterministic for the existing developer tools.
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--choose-rom")) {
        auto ui = srw64::app::macos_desktop_ui();
        const bool choose_another = argc == 2 || srw64::app::macos_choose_another_rom();
        // An internal test build carries the ROM (package_macos.py --rom): it answers the
        // first time the ROM picker would open, so a Mac that remembers another ROM keeps
        // it. Option or --choose-rom still asks.
        if (const auto bundled = srw64::app::bundled_resource("rom.z64"); !bundled.empty() && !choose_another)
            ui.choose_rom = [bundled, picker = ui.choose_rom, used = false]() mutable -> std::optional<std::filesystem::path> {
                if (!used) { used = true; return bundled; }
                return picker();
            };
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
            }, choose_another);
    }
#endif
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
#ifdef _WIN32
    const auto native_ui=srw64::app::windows_desktop_ui();
    const auto prepare_launcher=srw64::app::prepare_windows_launch;
    const auto named_rom=srw64::app::windows_named_rom;
#else
    // Without kdialog or zenity, errors still reach the player in the game's own box.
    const auto native_ui=srw64::app::linux_desktop_ui(srw64_error_box);
    const auto prepare_launcher=srw64::app::prepare_linux_launch;
    const auto named_rom=srw64::app::linux_named_rom;
#endif
    // Explorer supplies no arguments: start as Marchwind64.cmd does, which stays for
    // its options. --play and the diagnostic ABI below keep their console.
    if (argc==1 || (argc==2 && std::string_view(argv[1])=="--choose-rom")) {
        const auto& ui = native_ui;
        srw64::app::Options options;
        try {
            options = prepare_launcher(options,
                srw64::app::executable_path().parent_path(), {},
                argc == 1 ? named_rom() : std::filesystem::path{});
        } catch (const std::exception& error) {
            ui.show_error(std::string("Unable to start Marchwind64.\n") + error.what());
            return 2;
        }
        srw64::app::GameIdentity game{"srw64-jp-rev0", native_jp_sha256,
            native_rom_variants[0].save_file, srw64::rules::version, {}};
        for (const auto& rule : srw64::rules::catalog)
            game.rules.push_back({std::string(rule.id), rule.kind == srw64::rules::Kind::correction});
        return srw64::app::run_desktop(options, game.rom_sha256, ui,
            [&](const srw64::app::Options& selected, const srw64::app::DesktopReady& ready) {
                return srw64::app::run_standalone(selected, game, [&](int count, char** values) {
                    ready(); // The standalone Session owns the lock until run_host returns.
                    return run_host(count, values);
                });
            }, argc == 2);
    }
#endif
#ifdef __SWITCH__
    // The Switch (docs/design/switch-port.md): hbmenu passes only the NRO's path. Its folder,
    // /switch/Marchwind64 on the SD card, holds the program's files beside it (fonts,
    // dialogue, stages), the player's rom.z64 and the data made from it on the first launch
    // (game/: the ROM, the art pack, the dialogue data; app/console_data.hpp). After that,
    // nothing is checked, imported or rewritten at start. The game keeps its saves and runs
    // there too.
    if (argc <= 1) {
        // What the player reads when the game cannot start, in the console's language.
        const std::string language = srw64_switch_language();
        const auto say = [&](const char* zh, const char* ja, const char* en) -> std::string {
            return language.rfind("zh", 0) == 0 ? zh : language.rfind("ja", 0) == 0 ? ja : en;
        };
        const std::string cannot_start = say("Marchwind64 无法启动。", "Marchwind64 を起動できませんでした。", "Marchwind64 could not start.");
        srw64::app::set_boot_listener(srw64_switch_boot_line);
        srw64::app::Options options;
        options.user_dir = srw64::app::executable_path().parent_path();
        if (options.user_dir.empty()) options.user_dir = "/switch/Marchwind64";
        // /switch/Marchwind64/debug: the debug interface, on every address at port 46464
        // with the token in that file (debug_transport.cpp, tools/release/switch/attach.py).
        const auto present = srw64::app::list_names(options.user_dir);
        options.mute = present.contains("mute");
        options.debug = present.contains("debug");
        // From the album (a library applet) a homebrew program gets about 370 MiB, as much as
        // the PS Vita build lives in; an application (R held on a game) gets about 3 GiB.
        // Allowed with the debug file, for a console with no game to hold R on.
        if (!srw64_switch_application()) {
            std::fprintf(stderr, "SRW64_SWITCH_APPLET started from the album: library applet memory\n");
            if (!options.debug) {
                srw64_switch_error(cannot_start, say(
                    "请按住 R 键启动任意一个游戏，在打开的 hbmenu 里再启动 Marchwind64。从相簿打开的 hbmenu 内存不够。",
                    "R ボタンを押しながら任意のゲームを起動し、開いた hbmenu から Marchwind64 を起動してください。"
                    "アルバムから開いた hbmenu ではメモリが足りません。",
                    "Hold R while starting any game, then start Marchwind64 from the hbmenu that opens. "
                    "hbmenu opened from the Album has too little memory."));
                return 2;
            }
        }
        srw64::app::GameIdentity game{"srw64-jp-rev0", native_jp_sha256,
            native_rom_variants[0].save_file, srw64::rules::version, {}};
        for (const auto& rule : srw64::rules::catalog)
            game.rules.push_back({std::string(rule.id), rule.kind == srw64::rules::Kind::correction});
        try {
            // The first launch, or the first of another release: the data from the player's ROM
            // (rom.z64 in the folder; once made, the copy in game/ serves the next release).
            if (!srw64::app::console_data_current(options.user_dir, game.rom_sha256)) {
                auto rom = options.user_dir / "rom.z64";
                if (!present.contains("rom.z64")) rom = options.user_dir / "game" / "rom.z64";
                if (!srw64::app::list_names(rom.parent_path()).contains("rom.z64")) {
                    std::fprintf(stderr, "SRW64_PLAY_FAILURE no rom.z64 in %s\n", options.user_dir.string().c_str());
                    srw64_switch_error(cannot_start, say("没有找到 ROM。请把日版《超级机器人大战 64》的 ROM 命名为 rom.z64，放进 ",
                        "ROM が見つかりません。日本版『スーパーロボット大戦64』の ROM を rom.z64 という名前で次のフォルダに置いてください：",
                        "No ROM found. Name the ROM of Super Robot Taisen 64 (Japan) rom.z64 and put it in ") +
                        options.user_dir.string() + say("。", "", "."));
                    return 2;
                }
                const auto text = srw64::app::bundled_resource("dialogue");
                if (text.empty()) throw std::runtime_error("The dialogue folder beside Marchwind64.nro is missing.");
                srw64::app::boot_step("prepare: start");
                const size_t problems = srw64::app::prepare_console_data(rom, options.user_dir, text, options.user_dir.string());
                if (problems) std::fprintf(stderr, "SRW64_SWITCH_PREPARED with %zu dialogue text problems\n", problems);
            }
            return srw64::app::run_prepared(options, game, run_host);
        } catch (const srw64::app::UnsupportedRom& refused) {
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s\n", refused.what());
            srw64_switch_error(cannot_start, say("rom.z64 不是支持的版本，需要日版 Rev 0（任意字节序）。",
                "rom.z64 は対応していないバージョンです。日本版 Rev 0（バイト順は問いません）が必要です。",
                "rom.z64 is not the supported version: it must be the Japanese Rev 0 (any byte order)."));
            return 2;
        } catch (const std::system_error& failure) {
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s (%s %d)\n", failure.what(), failure.code().category().name(), failure.code().value());
            srw64_switch_error(cannot_start, failure.what());
            return 2;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s\n", error.what());
            srw64_switch_error(cannot_start, error.what());
            return 2;
        }
    }
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::fputs(srw64::app::usage().c_str(), stdout);
        return 0;
    }
#if !defined(__ANDROID__) && !defined(__SWITCH__) && !defined(__vita__)
    // RecompLauncher's setup step (hd_prepare.hpp): `--prepare [--user-dir DIR]`, also after
    // --play, which marchwind64.sh puts before everything. No window: the HD pack into the
    // user directory a start would use, progress lines on stdout.
    {
        std::vector<std::string_view> args(argv + 1, argv + argc);
        if (!args.empty() && args[0] == "--play") args.erase(args.begin());
        if (!args.empty() && args[0] == "--prepare") {
            std::filesystem::path user_dir;
            if (args.size() == 3 && args[1] == "--user-dir") user_dir = std::filesystem::absolute(std::filesystem::path(args[2]));
            else if (args.size() != 1) {
                std::fprintf(stderr, "SRW64_PREPARE_FAILURE --prepare takes only --user-dir DIR\n");
                return 3;
            }
            try {
                if (user_dir.empty()) {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
                    user_dir = prepare_launcher({}, srw64::app::executable_path().parent_path(), {}, {}).user_dir;
#else
                    user_dir = srw64::app::default_user_dir();
#endif
                }
            } catch (const std::exception& error) {
                std::fprintf(stderr, "SRW64_PREPARE_FAILURE %s\n", error.what());
                return 3;
            }
            return srw64::hd::prepare(user_dir, std::cout);
        }
    }
#endif
    if (argc > 1 && std::string_view(argv[1]) == "--play") {
        srw64::app::Options options;
        try {
            if (argc == 3 && std::string_view(argv[2]) == "--help") {
                std::fputs(srw64::app::usage().c_str(), stdout);
                return 0;
            }
            std::vector<std::string_view> args;
            for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
            options = prepare_launcher(srw64::app::parse_options(args, true),
                srw64::app::executable_path().parent_path(), {}, named_rom());
#else
            options = srw64::app::parse_options(args);
#endif
            if (!options.export_save.empty()) {
                std::fprintf(stderr, "SRW64_SAVE_EXPORT %s\n", srw64::app::export_save(options).string().c_str());
                return 0;
            }
            if (options.rom.empty() && !options.import_save.empty()) {
                std::fprintf(stderr, "SRW64_SAVE_IMPORT %s\n", srw64::app::import_save(options).c_str());
                return 0;
            }
            srw64::app::GameIdentity game{"srw64-jp-rev0", native_jp_sha256,
                native_rom_variants[0].save_file, srw64::rules::version, {}};
            for (const auto& rule : srw64::rules::catalog)
                game.rules.push_back({std::string(rule.id), rule.kind == srw64::rules::Kind::correction});
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
            return srw64::app::run_desktop(options, game.rom_sha256, native_ui,
                [&](const srw64::app::Options& selected, const srw64::app::DesktopReady& ready) {
                    return srw64::app::run_standalone(selected, game, [&](int count, char** values) {
                        ready();
                        return run_host(count, values);
                    });
                });
#else
            return srw64::app::run_standalone(options, game, run_host);
#endif
        } catch (const srw64::app::RomRefused& error) {
            // Not the supported ROM: the game stops before it opens a window, which from a
            // launcher with no console (Marchwind64.cmd double-clicked, marchwind64.sh in the
            // Deck's Game Mode, Android) looked like a crash. Say why (a player, 0.4.9).
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s\n", error.what());
            srw64::console_log::flush_now();
            srw64_error_box(srw64::app::rom_refused_message(error.problem, error.path, srw64::app::player_locale(options)));
            return 2;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "SRW64_PLAY_FAILURE %s\n", error.what());
            return 2;
        }
    }
    return run_host(argc, argv);
}
