#!/usr/bin/env python3
"""Write the 32-bit (ARMv7, PS Vita) counterparts of the runtime sources (docs/design/psv-port.md, V0).

The pinned upstream checkouts stay unchanged, as with prepare_runtime_lifecycle.py: each
generated file is the pinned text with checked, whole-string replacements, recorded in a
manifest with before/after hashes. build/recomp/vita-runtime/ then holds:

  include/recomp.h            N64Recomp's recomp.h with DMULT/DMULTU for compilers without
                              __int128 (four 32x32 products instead of #error)
  include/librecomp/mods.hpp  the hook hash without the 64-bit size_t assertion, so the
                              header compiles where it is still included
  recomp.cpp                  the lifecycle recomp.cpp (prepare_runtime_lifecycle.py) with
                              RDRAM from calloc (16 MiB, no 4 GiB reservation), without the
                              mod system, the live recompiler and the recompiled heap
  librecomp/CMakeLists.txt    librecomp without mods.cpp, mod_*.cpp, config*.cpp, heap.cpp,
                              o1heap and the N64Recomp/LiveRecomp targets; its sources named
                              from the upstream checkout, recomp.cpp from here
  include/ultramodern/renderer_context.hpp
                              the window handle for the Vita (an SDL_Window*, as on Linux)
  include/lightweightsemaphore.h, include/blockingconcurrentqueue.h
                              moodycamel's semaphore on the Vita's kernel semaphores
                              (relative timeouts; pthread-embedded's sem_timedwait never
                              ended a short wait); the queue header copied so its sibling
                              include finds the patched one
  error_handling.cpp          std::_Exit where newlib has no std::quick_exit

The include directory goes before the upstream ones (the probe's CMakeLists.txt does this).

tools/release/vita/CMakeLists.txt uses these; prepare_runtime_lifecycle.py must have run.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
UPSTREAM = ROOT / "build/recomp/upstream"
OUTPUT = ROOT / "build/recomp/vita-runtime"
# RDRAM on the 32-bit targets: the game's 8 MiB (osMemSize), overlay segment 14 at
# 0x80400000 and the host's scratch area at 0x807F0000 all lie within it.
RDRAM_BYTES = 16 * 1024 * 1024


def sha256(text: str) -> str:
    return hashlib.sha256(text.encode()).hexdigest()


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise RuntimeError(f"Pinned runtime source differs: {old[:80]!r}")
    return source.replace(old, new)


def remove_between(source: str, start: str, end: str, digest: str) -> str:
    """Delete from the unique `start` up to (not including) the unique `end`; the removed
    text must hash to `digest`, so only the pinned block ever goes."""
    if source.count(start) != 1 or source.count(end) != 1:
        raise RuntimeError(f"Pinned runtime region anchors differ: {start[:60]!r}")
    begin, stop = source.index(start), source.index(end)
    if stop <= begin:
        raise RuntimeError(f"Pinned runtime region order differs: {start[:60]!r}")
    removed = source[begin:stop]
    if sha256(removed) != digest:
        raise RuntimeError(f"Pinned runtime region content differs: {start[:60]!r} ({sha256(removed)})")
    return source[:begin] + source[stop:]


def pinned(checkout: Path, relative: str) -> str:
    marker = checkout / ".srw64-revision"
    if not (checkout / ".git").exists() and marker.is_file():
        return (checkout / relative).read_text()
    text = subprocess.check_output(["git", "show", f"HEAD:{relative}"], cwd=checkout, text=True)
    if (checkout / relative).read_text() != text:
        raise RuntimeError(f"{relative} has local changes")
    return text


def adapt_recomp_h(source: str) -> str:
    return replace_once(source, '''#else
#error "128-bit integer type not found"
#endif
''', '''#else

// 32-bit targets (ARMv7, docs/design/psv-port.md): the 128-bit product from four 32x32
// multiplications. The signed product follows from the unsigned one of the same bits:
// a*b = au*bu - 2^64*(a<0 ? bu : 0) - 2^64*(b<0 ? au : 0) (mod 2^128).
static inline void DMULTU(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    uint64_t a_lo = (uint32_t)a, a_hi = a >> 32, b_lo = (uint32_t)b, b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
    uint64_t middle = (p0 >> 32) + (uint32_t)p1 + (uint32_t)p2;
    *lo64 = (middle << 32) | (uint32_t)p0;
    *hi64 = p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32);
}

static inline void DMULT(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    uint64_t lo, hi;
    DMULTU((uint64_t)a, (uint64_t)b, &lo, &hi);
    if (a < 0) hi -= (uint64_t)b;
    if (b < 0) hi -= (uint64_t)a;
    *lo64 = (int64_t)lo;
    *hi64 = (int64_t)hi;
}

#endif
''')


def adapt_mods_hpp(source: str) -> str:
    return replace_once(source, '''        // This hash packing only works if the resulting value is 64 bits.
        static_assert(sizeof(std::size_t) == 8);
        // Combine the three values into a single 64-bit value.
        // The lower 2 bits of a function address will always be zero, so pack
        // the value of at_return into the lowest bit.
        return (size_t(def.section_rom) << 32) | size_t(def.function_vram) | size_t(def.at_return ? 1 : 0);''',
'''        // The 64-bit packing where size_t allows it; a mixed hash on 32-bit targets
        // (docs/design/psv-port.md), where the mod system is not built anyway.
        if constexpr (sizeof(std::size_t) == 8) {
            return (size_t(def.section_rom) << 32) | size_t(def.function_vram) | size_t(def.at_return ? 1 : 0);
        } else {
            return size_t(def.section_rom * 0x9E3779B1u) ^ size_t(def.function_vram) ^ size_t(def.at_return ? 1 : 0);
        }''')


def adapt_recomp_cpp(source: str) -> str:
    source = replace_once(source, '#include "recompiler/live_recompiler.h"\n', '')
    source = replace_once(source, '''#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <Windows.h>
#else
#    include <sys/mman.h>
#endif
''', '')
    source = replace_once(source, '''// The global mod context.
std::unique_ptr<recomp::mods::ModContext> mod_context = std::make_unique<recomp::mods::ModContext>();
''', '')
    source = replace_once(source, '''    if (!entry.mod_game_id.empty()) {
        std::lock_guard<std::mutex> lock(mod_context_mutex);
        mod_context->register_game(entry.mod_game_id);
    }
    return true;
}
''', '''    return true;
}
''')
    # The mod API wrappers, from initialize_mods() to the last one before the ROM helpers.
    source = remove_between(source, 'void recomp::mods::initialize_mods() {\n',
                            'bool check_hash(const std::vector<uint8_t>& rom_data, uint64_t expected_hash) {',
                            '21d8a716e30f4829856ea812222fd3a467a145c0c07ec9abcccaac9098df5d0a')
    source = replace_once(source, '''    game_status.notify_all();
    mods::set_latest_game_mode_id(game_mode_id);
}
''', '''    game_status.notify_all();
}
''')
    source = remove_between(source, 'void recomp::mods::enable_mod(const std::string& mod_id, bool enabled) {\n',
                            'bool wait_for_game_started(uint8_t* rdram, recomp_context* context) {',
                            '2291a41d6f2b246effe7b241e123396c963cd4777164c1c185d982545fd5fcea')
    source = remove_between(source, '                uint32_t mod_ram_used = 0;\n',
                            '                save_type = game_entry.save_type;',
                            '3a978b80918deb1f1e395814a0ee7229d5f921fbeef710fa7e74cba788232e52')
    source = replace_once(source, '''void print_cli_game_mode_options(const std::vector<recomp::mods::ModDetails> &mods) {
    for (const auto &mod : mods) {
        if (mod.custom_gamemode) {
            bool mod_enabled = recomp::mods::is_mod_enabled(mod.mod_id) || recomp::mods::is_mod_auto_enabled(mod.mod_id);
            fprintf(stderr, "\\"%s\\" (%s)%s\\n", mod.mod_id.c_str(), mod.display_name.c_str(), mod_enabled ? "" : " (Disabled)");
        }
    }
}

''', '')
    source = remove_between(source, '''            if (!cli_game_mode_id.empty() || game_mode_missing_argument) {
                std::vector<recomp::mods::ModDetails> mods = recomp::mods::get_all_mod_details(cli_game_id);
''', '''            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''', '9f8749b19981c102d2b45428c46fb411417de489591da0b31a52890a04169ab0')
    source = replace_once(source, '''            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''', '''            if (!cli_game_mode_id.empty() || game_mode_missing_argument) {
                fprintf(stderr, "Game modes need the mod system, which this build leaves out.\\n");
                game_mode_error = true;
            }
            if (!game_mode_error) {
                recomp::start_game(game_id, game_mode_id);
''')
    source = replace_once(source, '''    recomp::mods::initialize_mods();
    recomp::mods::scan_mods();

    // Allocate rdram without comitting it. Use a platform-specific virtual allocation function
    // that initializes to zero. Protect the region above the memory size to catch accesses to invalid addresses.
    uint8_t* rdram;
    bool alloc_failed;
#ifdef _WIN32
    rdram = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, allocation_size, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    DWORD old_protect = 0;
    alloc_failed = (rdram == nullptr);
    if (!alloc_failed) {
        // VirtualProtect returns 0 on failure.
        alloc_failed = (VirtualProtect(rdram, mem_size, PAGE_READWRITE, &old_protect) == 0);
        if (alloc_failed) {
            VirtualFree(rdram, 0, MEM_RELEASE);
        }
    }
#else
    rdram = (uint8_t*)mmap(NULL, allocation_size, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    alloc_failed = rdram == reinterpret_cast<uint8_t*>(MAP_FAILED);
    if (!alloc_failed) {
        // mprotect returns -1 on failure.
        alloc_failed = (mprotect(rdram, mem_size, PROT_READ | PROT_WRITE) == -1);
        if (alloc_failed) {
            munmap(rdram, allocation_size);
        }
    }
#endif
''', f'''    // RDRAM as a plain zeroed allocation (docs/design/psv-port.md): no 4 GiB reservation,
    // no guard pages; the game's 8 MiB and the host's areas below 16 MiB are all it uses.
    uint8_t* rdram = (uint8_t*)calloc(srw64_rdram_bytes, 1);
    bool alloc_failed = rdram == nullptr;
''')
    source = replace_once(source, '''    recomp::register_heap_exports();
    recomp::mods::register_config_exports();
    recomp::mods::register_hook_exports();
''', '')
    source = replace_once(source, '''    bool free_failed;
#ifdef _WIN32
    // VirtualFree returns zero on failure.
    free_failed = (VirtualFree(rdram, 0, MEM_RELEASE) == 0);
#else
    // munmap returns -1 on failure.
    free_failed = (munmap(rdram, allocation_size) == -1);
#endif
''', '''    std::free(rdram);
    bool free_failed = false;
''')
    return replace_once(source, 'enum GameStatus {', f'''// docs/design/psv-port.md: RDRAM on the 32-bit targets.
constexpr size_t srw64_rdram_bytes = {RDRAM_BYTES};

enum GameStatus {{''')


def adapt_renderer_context(source: str) -> str:
    # Its neighbours by their include-path names: this copy lives in another directory.
    source = replace_once(source, '#include "ultra64.h"\n#include "config.hpp"\n',
                          '#include "ultramodern/ultra64.h"\n#include "ultramodern/config.hpp"\n')
    return replace_once(source, '''#elif defined(__linux__) || defined(__ANDROID__)
        using WindowHandle = SDL_Window*;''', '''#elif defined(__linux__) || defined(__ANDROID__) || defined(__vita__)
        using WindowHandle = SDL_Window*;''')


VITA_SEMAPHORE = """#elif defined(__vita__)
//---------------------------------------------------------
// Semaphore (PS Vita): the kernel's own, with a relative timeout. pthread-embedded's
// sem_timedwait takes an absolute CLOCK_REALTIME time that its own clock reads hours
// apart from newlib's, so a short wait (an N64 timer) never ended.
//---------------------------------------------------------
class Semaphore
{
private:
	SceUID m_sema;

	Semaphore(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;
	Semaphore& operator=(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;

public:
	Semaphore(int initialCount = 0)
	{
		assert(initialCount >= 0);
		m_sema = sceKernelCreateSema("moodycamel", 0, initialCount, 0x7fffffff, nullptr);
		assert(m_sema >= 0);
	}

	~Semaphore()
	{
		sceKernelDeleteSema(m_sema);
	}

	bool wait()
	{
		return sceKernelWaitSema(m_sema, 1, nullptr) == 0;
	}

	bool try_wait()
	{
		return sceKernelPollSema(m_sema, 1) == 0;
	}

	bool timed_wait(std::uint64_t usecs)
	{
		SceUInt timeout = usecs > 0xffffffffu ? 0xffffffffu : static_cast<SceUInt>(usecs);
		return sceKernelWaitSema(m_sema, 1, &timeout) == 0;
	}

	void signal()
	{
		sceKernelSignalSema(m_sema, 1);
	}

	void signal(int count)
	{
		if (count > 0) sceKernelSignalSema(m_sema, count);
	}
};
"""


def adapt_semaphore(source: str) -> str:
    if source.count("#elif defined(__unix__)") != 2:
        raise RuntimeError("Pinned lightweightsemaphore.h differs")
    source = replace_once(source, "#elif defined(__unix__)\n#include <semaphore.h>",
                          "#elif defined(__vita__)\n#include <psp2/kernel/threadmgr.h>\n"
                          "#elif defined(__unix__)\n#include <semaphore.h>")
    return replace_once(source, "#elif defined(__unix__)\n//----", VITA_SEMAPHORE + "#elif defined(__unix__)\n//----")


def adapt_error_handling(source: str) -> str:
    return replace_once(source, '''#ifdef __APPLE__
    std::_Exit(exit_status);''', '''#if defined(__APPLE__) || defined(__vita__)
    std::_Exit(exit_status);''')


def adapt_librecomp_cmake(source: str) -> str:
    for removed in ("mods.cpp", "mod_events.cpp", "mod_hooks.cpp", "mod_manifest.cpp", "mod_config_api.cpp",
                    "config_option.cpp", "config.cpp", "heap.cpp"):
        source = replace_once(source, f'    "${{CMAKE_CURRENT_SOURCE_DIR}}/src/{removed}"\n', '')
    source = replace_once(source, '    "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/o1heap/o1heap/o1heap.c"\n', '')
    source = replace_once(source, '    "${CMAKE_CURRENT_SOURCE_DIR}/src/recomp.cpp"\n',
                          '    "${SRW64_VITA_RUNTIME}/recomp.cpp"\n')
    source = source.replace('${CMAKE_CURRENT_SOURCE_DIR}', '${SRW64_LIBRECOMP_SOURCE}')
    source = source.replace('${PROJECT_SOURCE_DIR}/..', '${SRW64_LIBRECOMP_SOURCE}/..')
    source = replace_once(source, '''target_include_directories(librecomp PUBLIC
    "${SRW64_LIBRECOMP_SOURCE}/include"
''', '''target_include_directories(librecomp PUBLIC
    "${SRW64_VITA_RUNTIME}/include"
    "${SRW64_LIBRECOMP_SOURCE}/include"
    "${SRW64_LIBRECOMP_SOURCE}/../N64Recomp/include"
''')
    source = replace_once(source, '''add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../thirdparty/miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz)
add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../N64Recomp ${CMAKE_CURRENT_BINARY_DIR}/N64Recomp EXCLUDE_FROM_ALL)

target_link_libraries(librecomp PRIVATE ultramodern N64Recomp LiveRecomp)
''', '''add_subdirectory(${SRW64_LIBRECOMP_SOURCE}/../thirdparty/miniz ${CMAKE_CURRENT_BINARY_DIR}/miniz)

target_link_libraries(librecomp PRIVATE ultramodern)
''')
    return ('# Generated by tools/recomp/toolchain/prepare_vita_runtime.py from librecomp/CMakeLists.txt;\n'
            '# SRW64_LIBRECOMP_SOURCE and SRW64_VITA_RUNTIME come from the including project.\n' + source)


def main() -> None:
    lock = json.loads((ROOT / "config/recomp/toolchain.json").read_text())["sources"]
    runtime = UPSTREAM / "N64ModernRuntime"
    recomp = UPSTREAM / "N64Recomp"
    lifecycle = json.loads((ROOT / "build/recomp/runtime-lifecycle/manifest.json").read_text())
    if lifecycle["revision"] != lock["N64ModernRuntime"]["commit"]:
        raise RuntimeError("Runtime lifecycle sources were generated from another revision")
    lifecycle_recomp = next(c for c in lifecycle["changes"] if c["source"] == "librecomp/src/recomp.cpp")
    lifecycle_source = (ROOT / lifecycle_recomp["generated"]).read_text()
    if sha256(lifecycle_source) != lifecycle_recomp["after_sha256"]:
        raise RuntimeError("Runtime lifecycle recomp.cpp differs from its manifest")
    inputs = [
        ("include/recomp.h", recomp, "include/recomp.h", adapt_recomp_h),
        ("include/librecomp/mods.hpp", runtime, "librecomp/include/librecomp/mods.hpp", adapt_mods_hpp),
        ("recomp.cpp", None, lifecycle_recomp["generated"], adapt_recomp_cpp),
        ("librecomp/CMakeLists.txt", runtime, "librecomp/CMakeLists.txt", adapt_librecomp_cmake),
        ("include/ultramodern/renderer_context.hpp", runtime, "ultramodern/include/ultramodern/renderer_context.hpp", adapt_renderer_context),
        ("include/lightweightsemaphore.h", runtime, "thirdparty/concurrentqueue/lightweightsemaphore.h", adapt_semaphore),
        ("include/blockingconcurrentqueue.h", runtime, "thirdparty/concurrentqueue/blockingconcurrentqueue.h", lambda text: text),
        ("error_handling.cpp", runtime, "ultramodern/src/error_handling.cpp", adapt_error_handling),
    ]
    records = []
    for name, checkout, relative, adapt in inputs:
        source = lifecycle_source if checkout is None else pinned(checkout, relative)
        adapted = adapt(source)
        destination = OUTPUT / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_text() != adapted:
            destination.write_text(adapted)
        records.append({"source": relative if checkout is None else f"{checkout.name}/{relative}",
                        "generated": str(destination.relative_to(ROOT)),
                        "before_sha256": sha256(source), "after_sha256": sha256(adapted)})
    (OUTPUT / "manifest.json").write_text(json.dumps({
        "schema": "srw64.vita-runtime.v1", "rdram_bytes": RDRAM_BYTES,
        "revisions": {"N64Recomp": lock["N64Recomp"]["commit"], "N64ModernRuntime": lock["N64ModernRuntime"]["commit"]},
        "changes": records}, indent=2) + "\n")


if __name__ == "__main__":
    main()
