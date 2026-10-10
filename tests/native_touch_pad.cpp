// Touch controls by scene (src/host/touch_pad.hpp, docs/design/touch-controls.md).
#include "../src/host/touch_pad.hpp"
#include "../src/host/touch_scene.hpp"
#include "../src/host/dialogue_model.hpp"
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace srw64::touch_pad;
int checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) throw std::runtime_error(what);
}
// The Solana Seeker held sideways: 2670 x 1200 pixels at about 19.5 per millimetre.
const float mm = 19.5f, width = 2670.f, height = 1200.f;

void places() {
    const auto l = layout(width, height, mm);
    for (const auto& p : l.slots) {
        const float half_w = p.round ? p.r : p.w / 2, half_h = p.round ? p.r : p.h / 2;
        check(p.x - half_w >= 0 && p.x + half_w <= width && p.y - half_h >= 0 && p.y + half_h <= height, "a slot leaves the screen");
    }
    // No two slots overlap, with room for a thumb between them.
    for (size_t i = 0; i < slot_count; ++i)
        for (size_t j = i + 1; j < slot_count; ++j) {
            const auto &a = l.slots[i], &b = l.slots[j];
            const float ra = a.round ? a.r : std::max(a.w, a.h) / 2, rb = b.round ? b.r : std::max(b.w, b.h) / 2;
            const float d = std::hypot(a.x - b.x, a.y - b.y);
            check(d >= ra + rb + 2 * mm || (!a.round && !b.round && std::abs(a.x - b.x) >= (a.w + b.w) / 2 + 1.5f * mm), "two slots crowd each other");
        }
    check(l[Slot::Primary].x > width * .85f && l[Slot::Primary].y > height * .6f, "the primary button is not bottom right");
    check(l[Slot::Back].x < l[Slot::Primary].x && std::abs(l[Slot::Back].y - l[Slot::Primary].y) < mm, "back is not left of the primary");
    check(l[Slot::Top1].x < width / 2 && l[Slot::Top1].y < 8 * mm, "settings is not top left");
    check(l[Slot::Primary].r >= 6 * mm && l[Slot::Arc2].r >= 4.5f * mm, "buttons smaller than a thumb");
}

void scenes() {
    for (size_t i = 0; i < size_t(SceneId::Count); ++i) {
        const auto s = scene(SceneId(i));
        // A label for every button, and one slot per action.
        for (size_t a = 0; a < slot_count; ++a) {
            check(!s.slots[a].filled() == s.slots[a].label.empty(), "a button without a label, or a label without a button");
            check(!(s.slots[a].bits && !s.slots[a].command.empty()), "a button both holds keys and opens a page");
            for (size_t b = a + 1; b < slot_count; ++b)
                check(!s.slots[a].filled() || s.slots[a] != s.slots[b], "an action twice in one scene");
        }
        if (SceneId(i) == SceneId::Hidden) continue;
        check(s[Slot::Top1].bits == bits::Option || SceneId(i) == SceneId::Hidden, "a scene without the settings button");
        // Confirming is always the primary slot; going back always the back slot.
        if (s[Slot::Back].bits) check(s[Slot::Back].bits == (SceneId(i)==SceneId::FixedDialogue?bits::L:bits::B), "the back/history slot sends the wrong action");
    }
    check(scene(SceneId::Hidden).stick == Stick::None, "the all-touch windows have a stick");
    check(scene(SceneId::Page).stick == Stick::Corner && scene(SceneId::Page)[Slot::Primary].bits == bits::A &&
          scene(SceneId::Page)[Slot::Back].bits == bits::B, "our pages keep the stick, OK and back");
    check(scene(SceneId::Dialogue).tap_primary && scene(SceneId::Dialogue)[Slot::Primary].bits == bits::A, "dialogue does not page on a tap");
    check(scene(SceneId::MoveSelect)[Slot::Arc2].bits == bits::R, "farthest is not R");
    check(scene(SceneId::TitleRing)[Slot::Top3].command == "library-open" && scene(SceneId::TitleRing)[Slot::Top4].command == "viewer-open" &&
          !scene(SceneId::TitleRing)[Slot::Top4].bits, "the title's Library and Battle Viewer buttons");
    check(scene(SceneId::BattleScene)[Slot::Primary].bits == bits::R2, "skipping the battle is not the primary button");
}

void buttons() {
    const auto l = layout(width, height, mm);
    const auto map = scene(SceneId::MapIdle), dialogue = scene(SceneId::Dialogue);
    Fingers f;
    check(f.down(l, map, 1, l.rest_x + l.dpad_cell, l.rest_y), "D-pad");
    check(f.down(l, map, 2, l[Slot::Primary].x, l[Slot::Primary].y), "primary");
    check(f.buttons() == (bits::Right | bits::A), "stick and primary together");
    check(f.pressed(Slot::Primary), "primary drawn pressed");
    // A thumb slides from the primary to the back button.
    f.move(l, map, 2, l[Slot::Back].x, l[Slot::Back].y);
    check(f.buttons() == (bits::Right | bits::B), "slide to back");
    f.up(2);
    check(f.down(l, map, 3, l[Slot::Top4].x, l[Slot::Top4].y) && (f.buttons() & bits::R2), "next enemy");
    check(f.down(l, map, 4, l[Slot::Top1].x, l[Slot::Top1].y) && (f.buttons() & bits::Option), "settings");
    // An empty slot is no button: the map's top left second slot is free.
    check(!f.down(l, map, 5, l[Slot::Top2].x, l[Slot::Top2].y), "an empty slot took a touch");
    f.clear();
    // Dialogue: a tap anywhere outside the controls pages; the stick area still reads back.
    check(f.down(l, dialogue, 6, width * .6f, height * .4f) && f.buttons() == bits::A, "a tap does not page");
    f.up(6);
    check(f.down(l, dialogue, 7, l.rest_x, l.rest_y) && f.buttons() == 0, "the D-pad centre pages");
    check(f.up(7) && !f.up(7) && f.empty(), "release");
}

void dpad() {
    const auto l = layout(width, height, mm);
    const auto full = scene(SceneId::Other), page = scene(SceneId::FixedPage);
    Fingers f;
    const float x = l.rest_x, y = l.rest_y, step = l.dpad_cell;
    // Each arm takes effect on the first touch; no initial drag is needed.
    const struct {float dx, dy; uint32_t bit;} arms[] = {
        {0, -1, bits::Up}, {0, 1, bits::Down}, {-1, 0, bits::Left}, {1, 0, bits::Right}};
    for (const auto& arm : arms) {
        check(f.down(l, full, 1, x + step * arm.dx, y + step * arm.dy) && f.buttons() == arm.bit, "D-pad arm missed");
        check(f.stick()->cx == x && f.stick()->cy == y, "D-pad centre followed the finger");
        f.up(1);
    }
    check(f.down(l, full, 1, x, y) && f.buttons() == 0, "D-pad centre pressed a direction");
    f.move(l, full, 1, x + step, y);
    check(f.buttons() == bits::Right, "slide from centre to right");
    check(f.down(l, full, 2, l[Slot::Primary].x, l[Slot::Primary].y) && f.buttons() == (bits::Right | bits::A), "D-pad plus confirm");
    f.move(l, full, 1, x, y - step);
    check(f.buttons() == (bits::Up | bits::A), "slide between D-pad arms");
    f.move(l, full, 1, x + step, y + step);
    check(f.buttons() == bits::A, "D-pad corner did not release");
    f.move(l, full, 1, x + 4 * step, y);
    check(f.buttons() == bits::A, "outside D-pad did not release");
    f.clear();
    check(!f.down(l, full, 1, width * .3f, height * .5f), "D-pad captured the old floating stick area");
    check(!f.down(l, page, 1, x + step, y + step), "D-pad corner captured a page touch");
    check(!in_stick_area(l, scene(SceneId::Hidden), x, y), "D-pad on an all-touch window");
}
}

// The game's scenes from its state (touch_scene.hpp).
void recognition() {
    using srw64::touch_scene::decide;
    check(decide(7, 0, 0, 2, false) == SceneId::Opening && decide(7, 0, 0, 12, false) == SceneId::Opening, "opening, a demo's unit");
    check(decide(7, 0, 0, 2, false, true) == SceneId::Attract, "PRESS START");
    check(decide(7, 0, 0, 3, false) == SceneId::TitleRing, "title ring");
    check(decide(0x1D, 0, 0, 3, false) == SceneId::TitleRing && decide(0x1E, 0, 0, 3, false) == SceneId::TitleRing &&
          decide(0x1D, 0, 0, 2, false, true) == SceneId::Attract, "the title back from a demo");
    check(decide(7, 0, 0, 13, true) == SceneId::Dialogue, "prologue line");
    check(decide(7, 0, 0, 13, false) == SceneId::Prologue && decide(4, 0, 0, 13, false) == SceneId::Prologue &&
          scene(SceneId::Prologue)[Slot::Arc2].bits == (bits::R | bits::Start), "prologue pages: next page and skip");
    check(decide(2, 5, 0, 0, true) == SceneId::BattleScene, "battle lines stay the battle's");
    check(decide(3, 5, 0, 0, false) == SceneId::MapIdle && decide(0x16, 6, 0, 0, false) == SceneId::MapIdle, "idle map, cursor moving");
    check(decide(3, 5, 0, 0, true) == SceneId::Dialogue, "a line on the map");
    check(decide(3, 8, 0, 0, false) == SceneId::MapMenu && decide(3, 0x3A, 0, 0, false) == SceneId::MapMenu, "unit menus");
    check(decide(3, 0xC, 0, 0, false) == SceneId::MoveSelect && decide(3, 0xC, 7, 0, false) == SceneId::MapMenu, "move select");
    check(decide(3, 0x17, 0, 0, false) == SceneId::TargetList, "weapons");
    check(decide(3, 0x30, 0, 0, false) == SceneId::InfoWindow, "information window");
    check(decide(3, 0x39, 0, 0, false) == SceneId::Other, "an unknown map state shows everything");
    check(decide(4, 0, 0, 0, false) == SceneId::Other && decide(4, 0, 0, 0, true) == SceneId::Dialogue, "intermission");
    check(decide(0x20, 0, 0, 0, false) == SceneId::AnyKey, "ending");
}

// Fixed buttons (by_scene off): the whole set everywhere, the stick in the corner on our
// pages, the title's two entries for L2 and R2; the settings window keeps none.
void fixed_buttons() {
    check(!by_scene, "buttons are fixed for now");
    const auto full = scene(SceneId::Other);
    for (const auto id : {SceneId::MapIdle, SceneId::BattleScene, SceneId::Opening, SceneId::AnyKey})
        check(fixed(id) == SceneId::Other, "a game scene shows the whole set");
    check(fixed(SceneId::Dialogue) == SceneId::FixedDialogue && fixed(SceneId::Prologue) == SceneId::FixedDialogue, "reading");
    for (const auto id : {SceneId::Page, SceneId::BattlePage, SceneId::BattleSpirits}) check(fixed(id) == SceneId::FixedPage, "our pages");
    check(fixed(SceneId::Attract) == SceneId::FixedTitle && fixed(SceneId::TitleRing) == SceneId::FixedTitle, "the title");
    check(fixed(SceneId::Hidden) == SceneId::Hidden, "the settings window");
    const auto page = scene(SceneId::FixedPage), title = scene(SceneId::FixedTitle), reading = scene(SceneId::FixedDialogue);
    check(page.stick == Stick::Corner && full.stick == Stick::Wide && title.stick == Stick::Wide, "sticks");
    for (size_t i = 0; i < slot_count; ++i) {
        check(page.slots[i] == full.slots[i], "the page has the whole set");
        if (i == size_t(Slot::Skip)) {
            check(!full.slots[i].filled() && !title.slots[i].filled() && reading.slots[i].filled(), "skip only while reading");
        } else check(full.slots[i].filled() && title.slots[i].filled() && reading.slots[i].filled(), "no empty place");
        if (i != size_t(Slot::Back) && i != size_t(Slot::Arc2) && i != size_t(Slot::Arc3) && i != size_t(Slot::Skip))
            check(reading.slots[i] == full.slots[i], "reading keeps the top row and other buttons");
    }
    check(reading[Slot::Arc2].bits == bits::L2 && reading[Slot::Arc2].label == "touch_auto" &&
          reading[Slot::Arc3].bits == bits::R2 && reading[Slot::Arc3].label == "touch_hold_fast", "round L1/R1 become auto and hold-fast");
    check(reading[Slot::Skip].bits == (bits::R | bits::Start) && reading[Slot::Skip].label == "touch_skip", "one finger skips with R plus START");
    const auto l = layout(width, height, mm);
    check(l[Slot::Skip].x == l[Slot::Arc3].x && l[Slot::Skip].y + l[Slot::Skip].r + 2 * mm < l[Slot::Arc3].y - l[Slot::Arc3].r,
          "skip is above R1 with space between touch targets");
    Fingers f;
    for (const auto slot : {Slot::Arc2, Slot::Arc3, Slot::Skip, Slot::Top2, Slot::Top3, Slot::Top4}) {
        const auto& place = l[slot];
        check(f.down(l, reading, 1, place.x, place.y) && f.buttons() == reading[slot].bits, "reading control sends its input");
        f.up(1);
        check(f.buttons() == 0, "hold-fast/skip sticks after release");
    }
    check(!f.down(l, full, 1, l[Slot::Skip].x, l[Slot::Skip].y), "skip area consumes touches outside dialogue");
    check(title[Slot::Top3].command == "library-open" && title[Slot::Top4].command == "viewer-open" &&
          title[Slot::Primary].bits == bits::A && title[Slot::Top2].bits == bits::Start, "the title's entries");
}

void reading_history() {
    const auto l=layout(width,height,mm);
    const auto reading=scene(fixed(SceneId::Dialogue),Reading::Active);
    const auto history=scene(fixed(SceneId::Dialogue),Reading::History);
    const auto skipping=scene(fixed(SceneId::Dialogue),Reading::Skipping);
    const auto prologue=scene(fixed(SceneId::Prologue),Reading::Inactive);
    check(reading[Slot::Back].bits==bits::L && reading[Slot::Back].label=="touch_history", "fixed dialogue has no history entry");
    check(history[Slot::Back].bits==bits::B && history[Slot::Back].label=="touch_back", "history has no return");
    check(skipping[Slot::Back].bits==bits::B && skipping[Slot::Back].label=="touch_stop_skip", "skip lost its cancellation");
    check(prologue[Slot::Back].bits==bits::B && prologue[Slot::Back].label=="touch_back", "prologue offers an unavailable history");
    for(const auto slot:{Slot::Top1,Slot::Top2,Slot::Top3,Slot::Top4})
        check(reading[slot]==scene(SceneId::Other)[slot], "reading changed the top row");
    Fingers fingers;
    for(const auto slot:{Slot::Arc2,Slot::Arc3,Slot::Skip,Slot::Top3,Slot::Top4}) {
        const auto& p=l[slot];
        check(!history[slot].filled() && !fingers.down(l,history,10,p.x,p.y), "a reading action is touchable over history");
    }
    using srw64::dialogue::Reader;
    srw64::dialogue::Layout text;
    text.text=u"甲乙丙";text.clusters={1,2,3};text.pages={{0,1,{}},{1,2,{}},{2,3,{}}};
    Reader reader;reader.begin(1,1,u"测试",text,0);
    const auto send=[&](uint64_t tick) {
        const auto held=fingers.buttons();
        return reader.update(uint16_t(held|((held&bits::R2)?Reader::R|Reader::A:0)),tick);
    };
    const auto& back=l[Slot::Back];
    check(fingers.down(l,reading,1,back.x,back.y) && !send(1) && reader.history_open, "touch cannot open history");
    fingers.reconcile(reading,history);
    check(fingers.owns(1) && fingers.buttons()==0 && !fingers.pressed(Slot::Back), "opening finger became return");
    fingers.move(l,history,1,l[Slot::Primary].x,l[Slot::Primary].y);
    check(!send(2) && reader.history_open, "blocked opening finger rearmed by dragging");
    fingers.up(1);
    reader.history_scroll_limit=30;
    check(fingers.down(l,history,2,l.rest_x,l.rest_y-l.dpad_cell) && !send(3) && reader.history_offset==1, "touch cannot scroll history");
    fingers.up(2);send(4);
    check(fingers.down(l,history,3,back.x,back.y) && !send(5) && !reader.history_open && reader.page==0, "return advances the line");
    fingers.reconcile(history,reading);
    check(!send(6) && !reader.history_open && reader.page==0, "return finger became a new history press");
    fingers.up(3);
    // Cancellation keeps its B semantics and cannot turn into history on release.
    reader.update(Reader::R|Reader::START,7);check(reader.skipping, "skip did not start");
    fingers.down(l,skipping,4,back.x,back.y);send(8);
    check(!reader.skipping && !reader.auto_read, "stop-skip did not cancel");
    fingers.reconcile(skipping,reading);send(9);
    check(!reader.history_open && fingers.owns(4), "stop-skip finger became history");
    fingers.up(4);
}
int main() {
    try { places(); scenes(); buttons(); dpad(); recognition(); fixed_buttons(); reading_history(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
