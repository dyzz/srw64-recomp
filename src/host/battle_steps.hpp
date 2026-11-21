#pragma once
// How the battle actor routines use their actor's scene, which is a resource number (+0x2C;
// docs/design/weapon-animation-survey.md §7.2), from the recompiled code by
// tools/content/export_weapon_animations.py (written there; tests/test_unit_maker.py keeps it the
// same as web/src/data/weapon-animations.json).
// - table: the routines that only move their actor to the scene resource k before or after its
//   own (801C5700 with +0x2C plus k), how far back and forward. A package picture on such an
//   actor needs copies of its scene that far around it (a sprite's "around", mod_units.hpp), or
//   the game reads the picture's atlas as a scene and crashes.
// - complex: the routines that compute with scene numbers any other way (compare them, keep them,
//   read another actor's). A record with one of these keeps the original's pictures: a package
//   picture anywhere in it (but a cut-in's) is refused.
#include <cstdint>
namespace srw64::battle_steps {
struct Reach { uint16_t behaviour; uint8_t before, after; };
inline constexpr Reach table[]={
    {12,0,4},{15,0,4},{16,0,1},{19,0,2},{24,0,1},{29,0,1},{30,0,2},{31,0,1},{35,0,2},{36,0,1},
    {39,0,2},{40,0,1},{42,0,4},{43,0,1},{46,0,1},{47,0,1},{57,0,1},{64,0,3},{69,0,2},{78,0,1},
    {79,0,1},{91,0,1},{93,0,1},{113,0,2},{115,0,1},{117,0,1},{118,0,2},{120,0,2},{134,0,4},
    {137,0,5},{139,0,1},{143,0,1},{148,0,8},{149,0,5},{151,0,1},{152,0,1},{155,0,1},{156,0,8},
    {163,0,4},{165,0,4},{166,0,4},{169,0,2},{170,0,6},{174,0,7},{177,0,6},{181,0,2},{186,0,1},
    {188,0,6},{191,0,6},{192,0,6},{193,0,2},{196,0,3},{198,0,1},{201,0,6},{202,0,1},{203,0,2},
    {204,0,2},{213,0,3},{214,0,2},{220,0,3},{239,0,1},{251,0,5},{252,0,2},{254,0,2},{255,0,16},
    {256,0,17},{257,0,5},{258,0,2},{261,0,1},{262,0,1},{264,0,1},{267,0,2},{268,0,5},{269,0,5},
    {271,0,5},{278,13,0},{279,0,12},{290,0,7},{291,0,1},{293,0,8},{294,0,9},{295,0,10},{296,0,1},
    {297,0,1},{303,0,1},{306,0,2},{313,0,3},{318,0,1},{327,0,4},{328,0,2},{331,0,6},{338,0,2},
    {341,0,1},{351,1,4},{353,0,6},{354,0,2},{368,0,1},{375,0,2},
};
inline constexpr uint16_t complex[]={
    11,48,49,60,63,65,77,88,98,104,112,114,126,127,141,153,157,158,164,167,168,171,172,173,175,176,
    178,179,180,189,190,199,200,205,211,219,248,249,266,270,276,287,288,292,298,300,304,305,324,325,
    333,334,336,345,346,347,349,356,
};
inline const Reach* of(unsigned behaviour) {
    for(const auto& r:table)if(r.behaviour==behaviour)return &r;
    return nullptr;
}
inline bool is_complex(unsigned behaviour) {
    for(const auto b:complex)if(b==behaviour)return true;
    return false;
}
}
