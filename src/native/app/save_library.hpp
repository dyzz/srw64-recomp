#pragma once
#include "sram.hpp"
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace srw64::app {
namespace fs = std::filesystem;
// The player's saves (docs/design/save-slots-autosave.md §2.1), in <user dir>/saves:
//   cartridge.sram       the 32 KiB card, byte for byte what ares keeps as save.ram
//   cartridge.sram.prev  the card before the last publication
//   slots/NNN.rec        slots 3..99: the 0x1F00-byte record the game writes to a slot
//   imports/             each card an import replaced
//   auto/inter-NNNNNN.rec, auto/turn-NNNNNN.sus  autosaves (slot / suspend format),
//                        numbered in one sequence so the newest is the highest
//   trash/               deleted slots and autosaves, with the .json beside them
//   import/, export/     files the player drops in for the game, and the game's exports
// Every file is replaced atomically. The caller holds the user directory's lock.
class SaveLibrary {
    fs::path root;
public:
    static constexpr unsigned first_slot=3, last_slot=99;
    explicit SaveLibrary(fs::path directory);
    const fs::path& directory() const { return root; }
    fs::path cartridge() const { return root/"cartridge.sram"; }
    // Nothing when there is no card yet; throws when the card is damaged.
    std::optional<sram::Bytes> read_cartridge() const;
    // Keeps the current card as .prev first. Throws for an unusable card.
    void publish_cartridge(std::span<const uint8_t> card);

    fs::path slot_path(unsigned number) const;
    std::vector<unsigned> slots() const;
    sram::Bytes read_slot(unsigned number) const;
    void write_slot(unsigned number,std::span<const uint8_t> record);
    // The lowest free number; throws when 3..99 are all taken.
    unsigned next_free_slot() const;
    // Copies a used record to the next free slot unless a slot already holds the
    // same bytes; returns the slot that holds it.
    unsigned keep(std::span<const uint8_t> record);
    // Before the card is replaced by `next`: keeps the current card's intact used
    // slots 1-2 that `next` does not also hold. A damaged card keeps what is intact.
    std::vector<unsigned> keep_cartridge_slots(std::span<const uint8_t> next);

    // A slot or suspend record of `size` bytes, used and with its checksum right.
    static void write_record(const fs::path& path,std::span<const uint8_t> record,size_t size);
    // Moves a record and its .json beside it to trash/; returns the new name.
    fs::path trash(const fs::path& record);

    enum class AutoKind { intermission, turn };
    struct AutoSave { AutoKind kind; unsigned sequence; fs::path record; };
    // Newest first.
    std::vector<AutoSave> autosaves() const;
    // The name for the next autosave of `kind`; nothing is written.
    fs::path next_autosave(AutoKind kind) const;
    // Keeps the newest `keep` of `kind`, records with their .json.
    void prune_autosaves(AutoKind kind,unsigned keep);

    struct Imported { sram::Detection detected; std::vector<unsigned> kept; fs::path backup; };
    // Replaces the card with an emulator file. The old card goes to imports/, its
    // used slots 1-2 to extended slots, and the seen bitmaps of both are merged.
    Imported import_cartridge(const fs::path& file);
    // One slot (index 0 or 1) of an emulator file into an extended slot. A record whose
    // checksum differs is refused unless `repair`, which stores it with the sum redone.
    unsigned import_slot(const fs::path& file,unsigned index,bool repair=false);
    // What an emulator file holds, slot by slot, for choosing what to import.
    struct Scan { sram::Detection detected; struct Record { bool used, intact; }; std::array<Record,2> slots; Record suspend; };
    static Scan scan(const fs::path& file);
    // Writes the card for an emulator. An existing destination is first copied
    // beside it; a RetroArch .srm keeps its other saves.
    fs::path export_cartridge(const fs::path& destination,sram::Format format) const;
    // Before this library the latest card was sessions/<id>/save.bin, named by
    // last-session.txt and checked against save.sha256. Copies it once; false
    // when there is nothing to migrate.
    bool migrate_sessions(const fs::path& user_dir);
};
}
