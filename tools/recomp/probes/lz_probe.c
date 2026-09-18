/* Isolated differential harness. These bindings are NOT a game runtime. */
#include "lz_probe_bindings.h"
#include "funcs.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { RAM_SIZE = 0x800000, DEST = 0x200000, HEAP = 0x600000, MAX_OUTPUT = 0x3fffc0 };
static uint8_t* rom;
static size_t rom_size;
static unsigned allocations, frees, reads;

static void fail(const char* message) {
    fprintf(stderr, "lz probe: %s\n", message);
    exit(1);
}

void probe_alloc(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    if (ctx->r4 != 0x400 || allocations >= 2) fail("unexpected allocation");
    ctx->r2 = (gpr)(int64_t)(int32_t)(0x80000000u + HEAP + allocations++ * 0x400);
}

void probe_free(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    if (frees >= 2 || (uint32_t)ctx->r4 != 0x80000000u + HEAP + frees * 0x400) fail("unexpected free");
    ++frees;
}

void probe_rom_read(uint8_t* rdram, recomp_context* ctx) {
    size_t offset = (uint32_t)ctx->r4;
    size_t destination = (uint32_t)ctx->r5 - 0x80000000u;
    size_t size = (uint32_t)ctx->r6;
    if (offset > rom_size || size > rom_size - offset) fail("ROM read out of bounds");
    if (destination != HEAP || size != 0x400) fail("unexpected ROM transfer");
    for (size_t i = 0; i < size; ++i) rdram[(destination + i) ^ 3] = rom[offset + i];
    ++reads;
}

int main(int argc, char** argv) {
    if (argc != 2) fail("expected ROM filename");
    FILE* input = fopen(argv[1], "rb");
    if (!input || fseek(input, 0, SEEK_END)) fail("cannot open ROM");
    long size = ftell(input);
    if (size != 0x2000000) fail("expected 32 MiB ROM");
    rewind(input);
    rom_size = (size_t)size;
    rom = malloc(rom_size);
    uint8_t* rdram = malloc(RAM_SIZE);
    if (!rom || !rdram || fread(rom, 1, rom_size, input) != rom_size) fail("cannot read ROM");
    fclose(input);
    char line[128];
    while (fgets(line, sizeof(line), stdin)) {
        unsigned offset, decoded_size;
        if (sscanf(line, "%x %u", &offset, &decoded_size) != 2 || decoded_size > MAX_OUTPUT) fail("invalid request");
        alarm(10);
        memset(rdram, 0, RAM_SIZE);
        for (unsigned i = DEST - 32; i < DEST + decoded_size + 32; ++i) rdram[i ^ 3] = 0xA5;
        recomp_context ctx = {0};
        ctx.r4 = offset;
        ctx.r5 = (gpr)(int64_t)(int32_t)(0x80000000u + DEST);
        ctx.r6 = decoded_size;
        ctx.r29 = (gpr)(int64_t)(int32_t)0x807ff000u;
        allocations = frees = reads = 0;
        srw64_lz_decode(rdram, &ctx);
        if (allocations != 2 || frees != 2 || (uint32_t)ctx.r29 != 0x807ff000u) fail("call state not restored");
        for (unsigned i = 0; i < 32; ++i) {
            if (rdram[(DEST - 32 + i) ^ 3] != 0xA5 || rdram[(DEST + decoded_size + i) ^ 3] != 0xA5) fail("output guard modified");
        }
        printf("%u %u %u %u\n", decoded_size, allocations, frees, reads);
        for (unsigned i = 0; i < decoded_size; ++i) putchar(rdram[(DEST + i) ^ 3]);
        fflush(stdout);
        alarm(0);
    }
    free(rom);
    free(rdram);
    return 0;
}
