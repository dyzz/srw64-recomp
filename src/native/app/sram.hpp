#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The game's 32 KiB battery SRAM and the emulator files that carry it
// (docs/design/save-slots-autosave.md §1). Pure byte handling; no files.
namespace srw64::app::sram {
using Bytes=std::vector<uint8_t>;
inline constexpr size_t size=0x8000;
// 0x0 header: the seven magic bytes, then the options byte (8009171C formats the
// whole card when the magic differs).
inline constexpr std::string_view magic="SRW64V3";
inline constexpr size_t slot_size=0x1F00, suspend_size=0x3AE0, seen_size=0xA0;
inline constexpr std::array<size_t,2> slot_offsets{0x10,0x1F10};
inline constexpr size_t suspend_offset=0x3E10, seen_offset=0x78F0;
// RetroArch mupen64plus-next .srm: EEPROM 0x800, four Controller Paks, SRAM, FlashRAM.
inline constexpr size_t retroarch_size=0x48800, retroarch_sram=0x20800;

// Big: the cartridge's own byte order (this host, ares). Word: each 32-bit word
// reversed (Project64, mupen64plus). Half: each 16-bit half reversed.
enum class Order { big, word, half };
enum class Format { ares, project64, mupen64plus, retroarch };
struct Detection { Order order; bool retroarch; };
// Which of the known layouts holds an SRW64 card; nothing for anything else.
std::optional<Detection> detect(std::span<const uint8_t> file);
// The card in the cartridge's byte order; throws when detect() finds nothing.
Bytes to_cartridge(std::span<const uint8_t> file);
// A file for the emulator. For RetroArch, `container` keeps the other saves of an
// existing .srm; an empty span starts a new one with them cleared.
Bytes from_cartridge(std::span<const uint8_t> cartridge,Format format,std::span<const uint8_t> container={});
std::optional<Format> format_for(std::string_view extension);
std::string_view name(Format format);
std::string_view name(Order order);

std::span<const uint8_t> slot(std::span<const uint8_t> cartridge,unsigned index);
std::span<uint8_t> slot(std::span<uint8_t> cartridge,unsigned index);
std::span<const uint8_t> suspend(std::span<const uint8_t> cartridge);
bool formatted(std::span<const uint8_t> cartridge);
// A slot or suspend record: +0 is a u16 with bit 15 set when used and the low
// byte the sum of the 0x1F00 bytes from +2. A slot's sum runs two bytes past the
// record into the intermission overlay's first bytes, always 00 00.
uint8_t checksum(std::span<const uint8_t> record);
bool used(std::span<const uint8_t> record);
bool intact(std::span<const uint8_t> record);
// What the load screen shows of a slot (offsets from the 801C2600 buffer).
struct Summary { uint8_t episode{}, title{}; uint16_t turns{}; uint32_t funds{}; };
Summary summary(std::span<const uint8_t> slot_record);
// Why a card cannot be used: wrong size, no magic, a used record whose checksum
// differs. Empty when it is fine.
std::vector<std::string> problems(std::span<const uint8_t> cartridge);
// The two "seen" bitmaps (characters, units) only ever gain bits.
void merge_seen(std::span<uint8_t> into,std::span<const uint8_t> from);
}
