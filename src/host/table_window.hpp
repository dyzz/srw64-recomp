#pragma once
// New unit and weapon numbers (docs/design/new-unit-ids.md §3): the tables the game reads by
// unit or weapon number are read at a virtual ROM window the host serves through 8007F704.
// The generator moves each reader's base there (generate_cpu.py TABLE_READERS; the layout
// is kept in step by tests/test_table_window.py). A table's block holds the ROM's rows at
// their own offsets, as the game reads them (upgrades::game_byte: the rules file's and the
// number MODs' bytes over the ROM's), then the rows of the new numbers, which exist only as
// the packages' bytes at window addresses (mod_units.hpp).
#include <cstdint>
#include <optional>

namespace srw64::table_window {
inline constexpr uint32_t window=0x08000000, block=0x00100000;
// map_icons is not read by the game here: the tactical map reads a copy in RDRAM the host
// fills from it (mod_tables.hpp), since the table lives in that overlay's data.
enum Table : unsigned {units, weapons, status_offsets, unit_songs, weapon_lists, poses, battle_units, battle_weapons,
                       scenes, anim_offsets, anim_records, names, map_icons,
                       pilot_records, thresholds, spirit_records, portraits, pilot_songs,
                       quote_segments, quote_index, quote_lists, table_count};
struct Layout { uint32_t rom, stride, rows; };
// The ROM's tables. The unit battle records have 354 rows; 354-362 read on into the weapon
// battle records, as in the original, so the block copies 363 of them.
inline constexpr Layout layouts[table_count]={
    {0x71B80,0x24,363},{0x74E90,0x10,1329},{0x7D980,4,363},{0x7DF30,2,363},
    {0x7E210,4,364},{0x84E40,6,365},{0x118610,14,363},{0x119970,14,1329},
    {0x11E3D0,6,1053},{0x11FC80,4,1329},{0,0,0},{0,0,0},{0x100D78,2,363},
    // New characters (docs/design/unit-mod-kit.md §5.4): a new character n has row n of its
    // ability record, skill thresholds and spirit learning (its own numbers), its portrait's
    // two resource numbers and its battle song.
    {0x7A1A0,0x10,257},{0x7B1B0,30,257},{0x7CFB0,12,148},{0x84220,4,361},{0x7D6A0,2,361},
    // Battle quotes by voice part (docs/data/battle-quotes.md; unit-mod-kit.md §7): the generic
    // segments (nine (start, count) pairs), the offset of each part's conditional list from
    // ROM 0x20F250, and the lists of the parts packages add (only package bytes, quote_list
    // apart each), which a new part's offset points into.
    {0x1161C0,36,257},{0x121150,4,257},{0,0,0},
};
// The character -> record maps the game keeps in resident RAM (s16 each, 360), moved to free
// RDRAM with room for the new characters (generate_cpu.py PILOT_MAP_READERS; mod_pilots.hpp
// fills them): the ability record for the stats, the same for the battle quotes (a new
// character borrows another's voice until it has its own), the spirit learning record.
inline constexpr uint32_t pilot_map_ram=0x800CA9C4, spirit_map_ram=0x800CA6F4, rom_pilot_maps=360;
inline constexpr uint32_t pilot_stats_copy=0x807D2000, pilot_voice_copy=0x807D2800, pilot_spirit_copy=0x807D3000, pilot_map_slots=1000;
// The love / friendship pairs (ROM 0x1012B4, in the tactical overlay at 80218754: 47 rows of
// 8 bytes, +2 holder, +4/+6 partners), read at a copy with room for the packages' rows
// (generate_cpu.py LOVE_READERS).
inline constexpr uint32_t love_rom=0x1012B4, love_copy=0x807D3800, love_rom_rows=47, love_rows=128, love_row=8;
// The tactical map's icon table in its overlay, and the copy its readers are moved to.
inline constexpr uint32_t map_icons_ram=0x80218218, map_icons_copy=0x807D0000, map_icon_slots=1024;
inline constexpr uint32_t base(Table t){return window+uint32_t(t)*block;}
inline constexpr uint32_t end=window+uint32_t(table_count)*block;
inline constexpr bool inside(uint32_t at){return at>=window && at<end;}

// The weapon lists: a u32 offset per unit from the table's start to rows of (weapon, five
// forms). The window keeps 1024 offsets at the start and the ROM's whole region (offsets
// and rows, 0x7E210-0x83110) after them, so a ROM offset reads 0x1000 further on.
inline constexpr uint32_t lists_rom_end=0x83110, lists_copy=0x1000, list_slots=0x400;
inline constexpr uint32_t empty_list=0x82E40;   // the ROM's 0xFF gap: a list that ends at once
// The animation records (anim_records): a weapon's offset is from the animation bank, so a
// new record at base(anim_records) + x has offset base(anim_records) + x - animation_bank.
inline constexpr uint32_t animation_bank=0x184990;
// The name records (names): 0x80 bytes a slot, one per private text id (name_record).
inline constexpr uint32_t name_slot=0x80;

// Numbers: the ROM's units and weapons, and the ranges new ones take (mod_ids.hpp). A saved
// unit number is ten bits (80092240) and 999 means "pilot only" to 3D5A; a weapon number
// 0x1000 is the animation editor's.
inline constexpr unsigned rom_units=363, rom_weapons=1329, rom_scenes=1053;
inline constexpr unsigned first_new_unit=400, last_new_unit=998, first_new_weapon=1400, last_new_weapon=2399;
// New characters: both save records keep a character in 9 bits (800923B4 and 80092E78 mask
// it with 0x1FF; the suspend record's 0x200 and 0x400 are flags), so 400-511.
inline constexpr unsigned rom_pilots=361, first_new_pilot=400, last_new_pilot=511;
inline constexpr unsigned new_scene_limit=4096;
// Voice parts: the ROM's 257, then one a new character with its own battle quotes speaks with
// (mod_quotes.hpp): character n has part 257 + n - 400.
inline constexpr unsigned rom_voices=257, first_new_voice=257, last_new_voice=first_new_voice+last_new_pilot-first_new_pilot;
inline constexpr uint32_t quote_lists_rom=0x20F250, quote_list=0x200;
// Resources (80089E9C): the ROM has 6,436; packages' own (mod_sprites.hpp) follow, served from
// a window of their own as LZSS streams (decoded size, then the stream); the loader's cap
// (sltiu 0x1924 at 80089EAC) is moved to resource_limit (generate_cpu.py).
inline constexpr unsigned rom_resources=6436, resource_limit=0x4000;
inline constexpr uint32_t resource_window=0x0A000000, resource_window_end=0x0C000000, resource_table=0x00A20BD0;

// Private text records for the names of new numbers (table 0 has 50,975; story MOD lines
// take 0xC800-0xE7FF): a character's name and full name, a unit's name, a weapon's name and
// its menu name.
inline constexpr uint16_t text_unit=527, text_weapon=1370, text_weapon_menu=2699, text_pilot=4382, text_pilot_full=4743;
inline constexpr uint16_t private_first=0xE800, private_pilot=0xE800, private_pilot_full=0xEC00;
inline constexpr uint16_t private_unit=0xF000, private_weapon=0xF400, private_weapon_menu=0xF800, private_end=0xFC00;
enum class NameKind : unsigned {unit=0, weapon=1, weapon_menu=2, pilot=3, pilot_full=4};
// A new unit's model line in the Library (names.json "models"): only the host reads it, the
// game has no such record for any unit.
inline constexpr uint16_t private_model=0xFC00;
inline constexpr uint16_t model_record(unsigned unit){return uint16_t(private_model+unit-first_new_unit);}
static_assert(model_record(last_new_unit)<0xFFFF);
// The text record of a unit's or a weapon's name, as the game asks for it.
inline constexpr uint16_t name_record(NameKind kind,unsigned number) {
    switch(kind) {
    case NameKind::unit:
        if(number<rom_units)return uint16_t(text_unit+number);
        if(number>=first_new_unit && number<=last_new_unit)return uint16_t(private_unit+number-first_new_unit);
        return uint16_t(text_unit+number);   // what the original would have asked for
    case NameKind::weapon: case NameKind::weapon_menu: {
        const uint16_t text=kind==NameKind::weapon?text_weapon:text_weapon_menu, own=kind==NameKind::weapon?private_weapon:private_weapon_menu;
        if(number<rom_weapons)return uint16_t(text+number);
        if(number>=first_new_weapon && number<=last_new_weapon)return uint16_t(own+number-first_new_weapon);
        return uint16_t(text+number);
    }
    case NameKind::pilot: case NameKind::pilot_full: {
        const uint16_t text=kind==NameKind::pilot?text_pilot:text_pilot_full, own=kind==NameKind::pilot?private_pilot:private_pilot_full;
        if(number>=first_new_pilot && number<=last_new_pilot)return uint16_t(own+number-first_new_pilot);
        return uint16_t(text+number);
    }
    }
    return 0;
}
inline constexpr uint16_t unit_name(unsigned number){return name_record(NameKind::unit,number);}
inline constexpr uint16_t weapon_name(unsigned number){return name_record(NameKind::weapon,number);}
inline constexpr uint16_t weapon_menu(unsigned number){return name_record(NameKind::weapon_menu,number);}
inline constexpr uint16_t pilot_name(unsigned number){return name_record(NameKind::pilot,number);}
inline constexpr uint16_t pilot_full_name(unsigned number){return name_record(NameKind::pilot_full,number);}
struct NameOf { NameKind kind; unsigned number; };
// The number a private record names.
inline constexpr std::optional<NameOf> name_of(uint16_t record) {
    if(record>=private_pilot && record<private_pilot+(last_new_pilot-first_new_pilot+1))return NameOf{NameKind::pilot,first_new_pilot+record-private_pilot};
    if(record>=private_pilot_full && record<private_pilot_full+(last_new_pilot-first_new_pilot+1))return NameOf{NameKind::pilot_full,first_new_pilot+record-private_pilot_full};
    if(record>=private_unit && record<private_weapon)return NameOf{NameKind::unit,first_new_unit+record-private_unit};
    if(record>=private_weapon && record<private_weapon_menu)return NameOf{NameKind::weapon,first_new_weapon+record-private_weapon};
    if(record>=private_weapon_menu && record<private_end)return NameOf{NameKind::weapon_menu,first_new_weapon+record-private_weapon_menu};
    return std::nullopt;
}
inline constexpr bool new_unit(unsigned n){return n>=first_new_unit && n<=last_new_unit;}
inline constexpr bool new_weapon(unsigned n){return n>=first_new_weapon && n<=last_new_weapon;}
inline constexpr bool new_pilot(unsigned n){return n>=first_new_pilot && n<=last_new_pilot;}
inline constexpr bool new_voice(unsigned n){return n>=first_new_voice && n<=last_new_voice;}
inline constexpr unsigned voice_of(unsigned pilot){return first_new_voice+pilot-first_new_pilot;}

// Where the game reads a table's row n: the ROM's own address for its rows (the copy
// patches are keyed there), the window for a new number.
inline constexpr uint32_t row(Table t,unsigned n) {
    const auto& l=layouts[t];
    if(t==weapon_lists)return base(t)+n*4;
    return n<l.rows?l.rom+n*l.stride:base(t)+n*l.stride;
}

// A window byte as the game reads it: `byte` reads a ROM address as the game does (patches
// over the ROM), `source` gives a new name record's source record (its own text record id
// and the table's offset and size of it). Patched window bytes are looked up before this.
template<class Byte,class Source>
inline uint8_t window_byte(uint32_t at,const Byte& byte,const Source& source) {
    const unsigned t=(at-window)/block;
    const uint32_t off=(at-window)%block;
    const auto& l=layouts[t];
    // Row -1 of the next table (a speaker with no face asks 8009C768 for portrait -1, which reads
    // the four bytes before the portraits: zero, no picture): the end of each block is the ROM
    // just before the next block's table, as the original read it. No table reaches that far.
    if(off>=block-0x1000 && t+1<table_count && layouts[t+1].rom)return byte(layouts[t+1].rom-(block-off));
    // A number no active package gives (a save made with one, loaded without it, until it is
    // turned back: mod_saves.hpp) reads row 0's bytes and an empty weapon list, so the game
    // never walks zeros as records.
    if(t==weapon_lists) {
        if(off<list_slots*4) {
            const unsigned unit=off/4;
            uint32_t value=empty_list-l.rom;
            if(unit<l.rows) {
                value=0;
                for(unsigned i=0;i<4;++i)value=value<<8|byte(l.rom+unit*4+i);
            }
            value+=lists_copy;
            return uint8_t(value>>(8*(3-off%4)));
        }
        if(off>=lists_copy && off<lists_copy+(lists_rom_end-l.rom))return byte(l.rom+off-lists_copy);
        return 0;
    }
    if(t==names) {
        const uint32_t slot=off/name_slot,within=off%name_slot;
        uint32_t rom=0,size=0;
        if(!source(uint16_t(private_first+slot),rom,size) || within>=size)return 0;
        return byte(rom+within);
    }
    if(l.stride && off<l.rows*l.stride)return byte(l.rom+off);
    // Past the ROM's rows: a new number (one no active package gives) reads row 0's bytes; any
    // other number reads on into the ROM after the table, as the original did (a speaker past
    // the portraits, a record past the thresholds).
    if(l.stride && l.rows) {
        const unsigned n=off/l.stride;
        const bool fresh=t==pilot_records || t==thresholds || t==spirit_records || t==portraits || t==pilot_songs?new_pilot(n):
                         t==weapons || t==battle_weapons || t==anim_offsets?new_weapon(n):
                         t==scenes?n<new_scene_limit:t==quote_segments || t==quote_index?new_voice(n):new_unit(n);
        return fresh?byte(l.rom+off%l.stride):byte(l.rom+off);
    }
    return 0;
}
}
