#ifndef SRW64_LZ_PROBE_BINDINGS_H
#define SRW64_LZ_PROBE_BINDINGS_H
#include "recomp.h"
void probe_alloc(uint8_t* rdram, recomp_context* ctx);
void probe_free(uint8_t* rdram, recomp_context* ctx);
void probe_rom_read(uint8_t* rdram, recomp_context* ctx);
#endif
