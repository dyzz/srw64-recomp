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
            // Two bars may sit side by side or one above the other (the screenshot under settings).
            check(d >= ra + rb + 2 * mm || (!a.round && !b.round && (std::abs(a.x - b.x) >= (a.w + b.w) / 2 + 1.5f * mm ||
                                                                      std::abs(a.y - b.y) >= (a.h + b.h) / 2 + 1.5f * mm)), "two slots crowd each other");
        }
    check(l[Slot::Primary].x > width * .85f && l[Slot::Primary].y > height * .6f, "the primary button is not bottom right");
    check(l[Slot::Back].x < l[Slot::Primary].x && std::abs(l[Slot::Back].y - l[Slot::Primary].y) < mm, "back is not left of the primary");
    check(l[Slot::Top1].x < width / 2 && l[Slot::Top1].y < 8 * mm, "settings is not top left");
    check(std::abs(l[Slot::Shot].x - l[Slot::Top1].x) < mm && l[Slot::Shot].y > l[Slot::Top1].y, "the screenshot is not under settings");
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
    check(!scene(SceneId::TitleRing)[Slot::Top3].filled() && !scene(SceneId::TitleRing)[Slot::Top4].filled(),
          "the title's Library and Battle Viewer are its own corner");
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
    // A thumb slides from the primary to the back button, whose B waits for letting go (its long press).
    f.move(l, map, 2, l[Slot::Back].x, l[Slot::Back].y);
    check(f.buttons() == bits::Right && f.find(2)->on_release == bits::B, "slide to back");
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
    // A gap between the arms is the D-pad's, with no direction: not a tap on the page.
    check(f.down(l, page, 1, x + step, y + step) && f.buttons() == 0, "D-pad corner gap reached the page");
    f.clear();
    check(!f.down(l, page, 1, x + 2.5f * step, y), "D-pad took a touch well outside its square");
    // A held arm keeps 1.5 mm more before letting go: a thumb rocking on the edge is one press.
    const float edge = step / 2 + .3f * l.mm;
    check(f.down(l, page, 1, x, y + edge) && f.buttons() == bits::Down, "down arm's inner edge missed");
    for (int i = 0; i < 6; ++i) {
        f.move(l, page, 1, x + (i % 2 ? .8f : -.8f) * l.mm, y + edge - (i % 2 ? 1.f : 0.f) * l.mm);
        check(f.buttons() == bits::Down, "rocking on the down arm's edge released it");
    }
    f.move(l, page, 1, x, y + step / 2 - 2 * l.mm);
    check(f.buttons() == 0, "back in the centre did not release");
    f.move(l, page, 1, x, y + step / 2 + .5f * l.mm);
    check(f.buttons() == bits::Down, "down arm again");
    f.move(l, page, 1, x, y + 1.5f * step + 2 * l.mm);
    check(f.buttons() == 0, "past the arm's end did not release");
    f.clear();
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
    // A story keeps hold to fast-forward through the gaps between lines (frontend.cpp).
    using srw64::touch_scene::story;
    check(story(12, 0x80) && story(13, 0), "the world map is story only");
    check(story(3, 0x2000) && story(0x11, 0) && !story(3, 0x80), "a map's event script runs; the player's map does not");
    check(!story(4, 0) && !story(2, 0) && !story(7, 0), "intermission, battle and title are not a map's story");
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
        // The game's screens add Back's long press (long_press()); the page has the rest as they are.
        auto plain = full.slots[i];
        plain.long_bits = 0;
        check(page.slots[i] == plain, "the page has the whole set");
        if (i == size_t(Slot::Skip)) {
            check(!full.slots[i].filled() && !title.slots[i].filled() && reading.slots[i].filled(), "skip only while reading");
        } else if (i == size_t(Slot::Info)) {
            check(full.slots[i].command == "stage-info-open" && !title.slots[i].filled(), "stage info everywhere but the title");
        } else if (i == size_t(Slot::Load)) {
            check(full.slots[i].command == "quick-load-ask" && !title.slots[i].filled(), "the quick load everywhere but the title");
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
    check(title[Slot::Top3].bits == bits::L2 && title[Slot::Top4].bits == bits::R2 && !title[Slot::Info].filled() &&
          title[Slot::Primary].bits == bits::A && title[Slot::Top2].bits == bits::Start, "the title's buttons");
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
// Back held opens the map overview (C-right): its B waits for a short press, on the game's own screens.
void long_press() {
    const auto full = scene(SceneId::Other);
    const auto l = layout(width, height, mm);
    check(full[Slot::Back].long_bits == bits::CRight && full[Slot::Back].bits == bits::B, "Back holds C-right");
    for (size_t i = 0; i < slot_count; ++i)
        if (Slot(i) != Slot::Back) check(!full.slots[i].long_bits, "only Back has a long press");
    for (const auto id : {SceneId::FixedPage, SceneId::FixedTitle, SceneId::FixedDialogue})
        check(!scene(id)[Slot::Back].long_bits, "our pages, the title and reading keep a plain Back");
    Fingers fingers;
    const auto& back = l[Slot::Back];
    check(fingers.down(l, full, 1, back.x, back.y) && fingers.buttons() == 0 && fingers.find(1)->on_release == bits::B,
          "a press on Back holds nothing until it is let go");
    check(fingers.lengthen(full, 1) && fingers.buttons() == bits::CRight && fingers.find(1)->on_release == 0, "long press holds C-right");
    check(!fingers.lengthen(full, 1), "a long press fires once");
    fingers.move(l, full, 1, l[Slot::Primary].x, l[Slot::Primary].y);
    check(fingers.buttons() == bits::CRight, "dragging off a long press changes it");
    fingers.up(1);
    check(fingers.down(l, full, 2, back.x, back.y), "Back again");
    fingers.move(l, full, 2, l[Slot::Primary].x, l[Slot::Primary].y);
    check(fingers.buttons() == bits::A && fingers.find(2)->on_release == 0, "a short press dragged onto A is A");
    fingers.up(2);
    const auto& a = l[Slot::Primary];
    check(fingers.down(l, full, 3, a.x, a.y) && !fingers.lengthen(full, 3) && fingers.buttons() == bits::A, "A has no long press");
    fingers.up(3);
    check(fingers.down(l, full, 4, back.x, back.y), "Back once more");
    fingers.reconcile(full, scene(SceneId::FixedPage));
    check(fingers.buttons() == 0 && fingers.find(4)->on_release == 0, "a scene change drops the waiting B");
}
// The player's layout (docs/design/touch-controls.md §8).
void custom_layout() {
    const auto plain = layout(width, height, mm);
    Customs customs{};
    check(layout(width, height, mm, customs).slots[0].x == plain.slots[0].x, "no custom is the default place");
    // A moved button follows the screen's size; its size stays physical.
    auto& ok = customs[size_t(Slot::Primary)];
    ok = {true, .25f, .5f, 150};
    auto l = layout(width, height, mm, customs);
    check(std::abs(l[Slot::Primary].x - width * .25f) < 1 && std::abs(l[Slot::Primary].y - height * .5f) < 1, "a moved button is not where it was put");
    check(std::abs(l[Slot::Primary].r - plain[Slot::Primary].r * 1.5f) < .01f, "a scaled button keeps the wrong size");
    const auto small = layout(width / 2, height / 2, mm, customs);
    check(std::abs(small[Slot::Primary].x - width / 8) < 1 && small[Slot::Primary].r == l[Slot::Primary].r, "a smaller screen rescales the place only");
    // Whatever was saved, every control stays on the screen.
    ok = {true, 1.f, 1.f, 200};
    customs[dpad_control] = {true, 0.f, 0.f, 200};
    l = layout(width, height, mm, customs);
    check(l[Slot::Primary].x + l[Slot::Primary].r <= width + 1 && l[Slot::Primary].y + l[Slot::Primary].r <= height + 1, "a button left the screen");
    check(l.rest_x >= 1.5f * l.dpad_cell - 1 && l.rest_y >= 1.5f * l.dpad_cell - 1, "the D-pad left the screen");
    // Moved D-pad: pressed where it is now, no longer where it was.
    customs = {};
    customs[dpad_control] = {true, .5f, .5f, 100};
    l = layout(width, height, mm, customs);
    const auto whole = scene(SceneId::Other);
    check(in_stick_area(l, whole, width / 2, height / 2) && !in_stick_area(l, whole, plain.rest_x, plain.rest_y), "the D-pad's area did not move");
    check(direction(l, l.rest_x, l.rest_y, l.rest_x, l.rest_y - l.dpad_cell) == bits::Up, "the moved D-pad lost its directions");
    // Two buttons on one spot: the nearer centre wins.
    customs = {};
    customs[size_t(Slot::Primary)] = {true, .5f, .5f, 100};
    customs[size_t(Slot::Back)] = {true, .52f, .5f, 100};
    l = layout(width, height, mm, customs);
    check(hit(l, whole, l[Slot::Back].x + mm, l[Slot::Back].y) == Slot::Back && hit(l, whole, l[Slot::Primary].x - mm, l[Slot::Primary].y) == Slot::Primary, "overlapping buttons go by slot order");
    for (const auto id : control_ids) check(!id.empty(), "a control without an id");
    const auto all = edit_scene();
    for (size_t i = 0; i < slot_count; ++i) check(all.slots[i].filled(), "the layout screen leaves out a control");
    // Every button the dialogue puts in another's place is named there too.
    const auto dialogue = scene(SceneId::FixedDialogue, Reading::Active);
    for (size_t i = 0; i < slot_count; ++i)
        if (whole.slots[i].filled() && dialogue.slots[i].filled() && dialogue.slots[i].label != whole.slots[i].label)
            check(!dialogue_label(i).empty(), "a dialogue button has no name on the layout screen");
}

void editor() {
    Editor edit{Customs{}};
    auto view = [&] { return layout(width, height, mm, edit.customs()); };
    auto l = view();
    const float x = l[Slot::Back].x, y = l[Slot::Back].y;
    // A tap chooses and moves nothing, not even with a thumb's roll.
    edit.press(l, x, y); edit.move(l, x + .5f * mm, y); edit.release();
    check(edit.selected() == size_t(Slot::Back) && !edit.customs()[size_t(Slot::Back)].moved, "a tap moved the button");
    // A drag moves it by the pointer's way, wherever on it the finger took it.
    edit.press(l, x + 2 * mm, y); edit.move(l, x + 2 * mm - 400, y - 300);
    check(edit.dragging(), "no drag");
    l = view();
    check(std::abs(l[Slot::Back].x - (x - 400)) < 1 && std::abs(l[Slot::Back].y - (y - 300)) < 1, "the button jumped under the finger");
    edit.release();
    check(!edit.dragging(), "still dragging");
    // Dragged off the edge it stops there, and comes back with the finger.
    edit.press(l, l[Slot::Back].x, l[Slot::Back].y); edit.move(l, -500, -500); edit.release();
    l = view();
    check(std::abs(l[Slot::Back].x - l[Slot::Back].r) < 1 && std::abs(l[Slot::Back].y - l[Slot::Back].r) < 1, "the edge did not stop the button");
    // The grid.
    edit.grid = true;
    edit.press(l, l[Slot::Back].x, l[Slot::Back].y); edit.move(l, 1003, 601); edit.release();
    l = view();
    const float step = grid_mm * mm;
    check(std::abs(std::remainder(l[Slot::Back].x, step)) < 1 && std::abs(std::remainder(l[Slot::Back].y, step)) < 1, "the grid did not catch the button");
    edit.grid = false;
    // The D-pad moves as one.
    edit.press(l, l.rest_x, l.rest_y); check(edit.selected() == dpad_control, "the D-pad was not chosen");
    edit.move(l, l.rest_x + 300, l.rest_y - 100); edit.release();
    const auto moved = view();
    check(std::abs(moved.rest_x - (l.rest_x + 300)) < 1 && std::abs(moved.rest_y - (l.rest_y - 100)) < 1, "the D-pad did not follow");
    // Size: the chosen control's, or with none chosen all of them.
    edit.set_scale(1000);
    check(edit.customs()[dpad_control].scale == max_scale && edit.customs()[0].scale == 100, "size went to the wrong control");
    edit.press(moved, width / 2, height / 2); edit.release();
    check(!edit.selected(), "empty screen chose something");
    check(edit.scale() == 100, "each control has its own size: the slider for all starts at 100");
    edit.set_scale(10);
    for (const auto& c : edit.customs()) check(c.scale == min_scale, "size did not reach all the controls");
    check(edit.scale() == min_scale, "the size they share");
    edit.reset();
    check(edit.customs() == Customs{} && !edit.selected(), "reset left something");
}
int main() {
    try { places(); scenes(); buttons(); dpad(); recognition(); fixed_buttons(); reading_history(); long_press(); custom_layout(); editor(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
