// Isolated task replay using N64ModernRuntime's actual RSP vector/DMA helpers.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>
#include "librecomp/rsp.hpp"

RspExitReason srw64_audio_probe(uint8_t* rdram, uint32_t ucode_addr);

static std::vector<std::pair<uint32_t, uint32_t>> writes;

void srw64_probe_dma_write(uint8_t* rdram, uint32_t dmem_address,
                          uint32_t dram_address, uint32_t encoded_length) {
    const uint32_t start = dram_address & 0xFFFFF8;
    const uint32_t size = encoded_length + 1;
    if (!size || size > 0x1000 || start > 0x800000 - size) std::abort();
    dma_dmem_to_rdram(rdram, dmem_address, dram_address, encoded_length);
    writes.emplace_back(start, start + size);
}

static RspUcodeFunc* select_ucode(const OSTask* task) {
    return task->t.type == 2 ? srw64_audio_probe : nullptr;
}

static void swap_words(std::vector<uint8_t>& bytes) {
    for (size_t offset = 0; offset < bytes.size(); offset += 4) {
        std::swap(bytes[offset], bytes[offset + 3]);
        std::swap(bytes[offset + 1], bytes[offset + 2]);
    }
}

int main(int argc, char** argv) {
    if (argc != 5) return 2;
    std::ifstream source(argv[1], std::ios::binary);
    if (!source) return 3;
    std::vector<uint8_t> memory{std::istreambuf_iterator<char>(source), {}};
    if (memory.size() != 0x800000) return 4;
    const size_t task_address = std::stoul(argv[2], nullptr, 0) & 0x1FFFFFFF;
    if ((task_address & 7) || task_address > memory.size() - sizeof(OSTask)) return 5;
    swap_words(memory);
    auto* task = reinterpret_cast<OSTask*>(memory.data() + task_address);
    recomp::rsp::constants_init();
    recomp::rsp::set_callbacks({select_ucode});
    alarm(10);
    bool success = recomp::rsp::run_task(memory.data(), task);
    alarm(0);
    if (!success) return 6;
    swap_words(memory);
    std::ofstream destination(argv[3], std::ios::binary);
    destination.write(reinterpret_cast<const char*>(memory.data()), memory.size());
    if (!destination) return 7;
    std::ofstream trace(argv[4]);
    trace << "{\"schema\":\"srw64.recomp-rsp-dma-writes.v1\",\"writes\":[";
    for (size_t i = 0; i < writes.size(); ++i) {
        if (i) trace << ",";
        trace << "{\"start\":" << writes[i].first << ",\"end\":" << writes[i].second << "}";
    }
    trace << "]}\n";
    if (!trace) return 8;
    std::cout << "audio task reached RSP BREAK\n";
    return 0;
}
