// Replay one captured SRW64 graphics task to isolate renderer/font experiments.
// This is a renderer component probe, not a new gameplay run or a save state.
#include "graphics.hpp"
#include "audio.hpp"
#include "replay_vi.hpp"
#include "ultramodern/ultramodern.hpp"
#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {
std::atomic<uint64_t> replay_ticks;
uint32_t be32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes.at(offset)) << 24 | uint32_t(bytes.at(offset + 1)) << 16 |
           uint32_t(bytes.at(offset + 2)) << 8 | bytes.at(offset + 3);
}
std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}
}

uint64_t srw64_current_vi() { return replay_ticks.load(); }

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::filesystem::path source = argv[1], output = argv[2];
    if (std::filesystem::exists(output)) return 2;
    std::filesystem::create_directories(output);
    auto original = read(source / "latest-gfx-rdram.bin");
    auto task_bytes = read(source / "latest-gfx-task.bin");
    if (original.size() != 0x800000 || task_bytes.size() != sizeof(OSTask)) return 2;
    OSTask task;
    std::memcpy(&task, task_bytes.data(), sizeof(task));
    const uint32_t start = uint32_t(task.t.data_ptr) & 0x1FFFFFFF;
    if (uint64_t(start) + task.t.data_size > original.size() || task.t.type != 1) return 2;
    uint32_t framebuffer = 0, width = 0;
    size_t color_command = 0;
    for (size_t i = start; i < start + task.t.data_size; i += 8) {
        const auto command = be32(original, i);
        if (command >> 24 == 0xFF) {
            width = (command & 0xFFF) + 1;
            framebuffer = be32(original, i + 4) & 0x1FFFFFFF;
            color_command = i;
        }
    }
    const uint32_t mode = 0x178A10 + 80 * original[0x10F5B2];
    auto* vi = ultramodern::renderer::get_vi_regs();
    const uint32_t table_status = be32(original, mode + 4);
    vi->VI_STATUS_REG = srw64_replay_vi_status(original.data(), original.size(), table_status);
    vi->VI_WIDTH_REG = be32(original, mode + 8);
    vi->VI_TIMING_REG = be32(original, mode + 12);
    vi->VI_V_SYNC_REG = be32(original, mode + 16);
    vi->VI_H_SYNC_REG = be32(original, mode + 20);
    vi->VI_LEAP_REG = be32(original, mode + 24);
    vi->VI_H_START_REG = be32(original, mode + 28);
    vi->VI_X_SCALE_REG = be32(original, mode + 32);
    vi->VI_ORIGIN_REG = framebuffer + be32(original, mode + 40);
    vi->VI_Y_SCALE_REG = be32(original, mode + 44);
    vi->VI_V_START_REG = be32(original, mode + 48);
    vi->VI_V_BURST_REG = be32(original, mode + 52);
    vi->VI_INTR_REG = be32(original, mode + 56);
    if (!framebuffer || width != vi->VI_WIDTH_REG) return 2;
    std::ofstream report(output / "frame-replay.json");
    report << "{\"schema\":\"srw64.native-frame-replay.v1\",\"evidence_scope\":\"single-task-renderer-replay\","
           << "\"vi_mode_source\":\"guest-mode-table-and-signature-verified-special-features\",\"mode_rdram\":" << mode
           << ",\"table_vi_status\":" << table_status << ",\"effective_vi_status\":" << vi->VI_STATUS_REG
           << ",\"special_features_callsite\":\"0x8008BFB4..0x8008BFD8\""
           << ",\"framebuffer\":" << framebuffer << ",\"width\":" << width
           << ",\"replay_second_framebuffer\":9437184}\n";
    // Alternate the captured target with scratch memory above the captured
    // RDRAM extent, so RT64 sees distinct VI presentations and can settle its
    // asynchronous uploads. No game code executes in this renderer replay.
    std::vector<uint8_t> rdram(0x1000000);
    for (size_t i = 0; i < original.size(); ++i) rdram[i ^ 3] = original[i];
    srw64_set_capture_directory(output);
    srw64_set_capture_clock("replay_iteration");
    srw64_configure_audio(false, output);
    auto window = srw64_create_window(nullptr);
    auto renderer = srw64_create_renderer(rdram.data(), window);
    if (!renderer->valid()) return 3;
    for (unsigned frame = 0; frame < 90; ++frame) {
        replay_ticks = frame;
        const uint32_t target = frame % 2 ? 0x900000 : framebuffer;
        std::memcpy(rdram.data() + color_command + 4, &target, sizeof(target));
        vi->VI_ORIGIN_REG = target + be32(original, mode + 40);
        SDL_PumpEvents();
        renderer->send_dl(&task);
        renderer->update_screen();
        SDL_Delay(17);
    }
    renderer->shutdown();
    renderer.reset();
    srw64_destroy_window();
    return 0;
}
