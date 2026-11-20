#pragma once
// Who says a battle quote. A quote's name and portrait come from its text record's speaker
// digits (8008F648 reads them into the panel's +0x20C: the name record 0x111E + digits, the
// portrait 8008F970(.., digits, ..)). A package's new character without lines of its own
// speaks with the voice part of the character it copies (table_window.hpp, the voice copy),
// so its quotes are that character's records and showed that character's name and face.
// When the battle overlay shows a quote (801C6F14, its argument the slot: a participant
// record's +0x2C or +0x848) and the slot's participant is a new character sharing the
// speaker's voice part, the host shows the participant for the speaker, for that one call.
#include "guest_memory.hpp"
#include "table_window.hpp"
#include <cstdint>
#include <optional>
#include <utility>

namespace srw64::battle_speaker {
using guest::read;

// The two participant records of the battle overlay (801F3578 fills them: +2 the voice
// part, quote slots at +0x2C and +0x848), the attacker's and the defender's record index,
// and their pilot pointers in the battle's participant table (+2 the character).
inline constexpr uint32_t records=0x800F97E0, record_size=0x1074, attack_slot=0x2C, hit_slot=0x848;
inline constexpr uint32_t attacker_index=0x80178C78, defender_index=0x80178B50;
inline constexpr uint32_t attacker_pilot=0x8018B6F4, defender_pilot=0x8018B750;

inline unsigned voice(const uint8_t* ram,unsigned character) {
    if(character>=table_window::pilot_map_slots)return 0xFFFF;
    return read(ram,table_window::pilot_voice_copy+2*character,2);
}
inline unsigned character_at(const uint8_t* ram,uint32_t pointer_at) {
    const uint32_t pointer=read(ram,pointer_at,4);
    return guest::valid(pointer,4)?read(ram,pointer+2,2):0;
}
// The character whose quote slot `slot` is, 0 when it is no participant's slot. The record's
// +2 is its voice part: of the attacker and the defender, the one with that voice part. The
// two index words follow the exchange (a counter swaps them) while the pilot pointers stay,
// so they only settle two participants of one voice part.
// The battle viewer's battle (battle_viewer.cpp) runs as the title's demo, without the map's
// participant table: it writes record 0 for the attacker and record 1 for the defender itself
// and says here whose they are while it runs.
struct Viewer { bool on=false; unsigned characters[2]{}; };
inline Viewer& viewer(){static Viewer value;return value;}

inline unsigned character_of_slot(const uint8_t* ram,uint32_t slot) {
    if(slot<records || slot>=records+2*record_size)return 0;
    const uint32_t index=(slot-records)/record_size,offset=(slot-records)%record_size;
    if(offset!=attack_slot && offset!=hit_slot)return 0;
    if(viewer().on)return viewer().characters[index];
    const unsigned part=read(ram,records+index*record_size+2,2);
    const unsigned attacker=character_at(ram,attacker_pilot),defender=character_at(ram,defender_pilot);
    const bool a=attacker && voice(ram,attacker)==part,d=defender && voice(ram,defender)==part;
    if(a!=d)return a?attacker:defender;
    if(!a)return 0;
    if(index==read(ram,attacker_index,2))return attacker;
    if(index==read(ram,defender_index,2))return defender;
    return 0;
}
// The speaker to show in place of `speaker` for `character`'s quote: the character, when it is
// a package's new one with no voice part of its own (it shares the speaker's).
inline std::optional<std::pair<unsigned,unsigned>> swap(const uint8_t* ram,unsigned speaker,unsigned character) {
    if(!character || character==speaker || character<table_window::rom_pilots)return std::nullopt;
    const unsigned v=voice(ram,character);
    if(v==0xFFFF || v!=voice(ram,speaker))return std::nullopt;
    return std::pair{speaker,character};
}

// Game thread: the swap of the quote being shown (801C6F14's call), else none.
struct Active { int from=-1,to=-1; };
inline Active& active(){static Active value;return value;}
inline unsigned shown(unsigned speaker) {
    const auto& a=active();
    return a.from>=0 && speaker==unsigned(a.from)?unsigned(a.to):speaker;
}
}
