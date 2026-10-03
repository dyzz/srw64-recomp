// Touch controls by scene (src/host/touch_pad.hpp, docs/design/touch-controls.md).
#include "../src/host/touch_pad.hpp"
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
    check(l[Slot::Top1].x < width / 2 && l[Slot::Top1].y < 10 * mm, "settings is not top left");
    check(l[Slot::Primary].r >= 7.5f * mm && l[Slot::Arc2].r >= 5.5f * mm, "buttons smaller than a thumb");
}

void scenes() {
    for (size_t i = 0; i < size_t(SceneId::Count); ++i) {
        const auto s = scene(SceneId(i));
        // A label for every button, and one slot per action.
        for (size_t a = 0; a < slot_count; ++a) {
            check(!s.slots[a].bits == s.slots[a].label.empty(), "a button without a label, or a label without a button");
            for (size_t b = a + 1; b < slot_count; ++b)
                check(!s.slots[a].bits || s.slots[a] != s.slots[b], "an action twice in one scene");
        }
        if (SceneId(i) == SceneId::Hidden) continue;
        check(s[Slot::Top1].bits == bits::Option || SceneId(i) == SceneId::Hidden, "a scene without the settings button");
        // Confirming is always the primary slot; going back always the back slot.
        if (s[Slot::Back].bits) check(s[Slot::Back].bits == bits::B, "the back slot is not B");
    }
    check(scene(SceneId::Hidden).stick == Stick::None, "the all-touch windows have a stick");
    check(scene(SceneId::Page).stick == Stick::Corner && scene(SceneId::Page)[Slot::Primary].bits == bits::A &&
          scene(SceneId::Page)[Slot::Back].bits == bits::B, "our pages keep the stick, OK and back");
    check(scene(SceneId::Dialogue).tap_primary && scene(SceneId::Dialogue)[Slot::Primary].bits == bits::A, "dialogue does not page on a tap");
    check(scene(SceneId::MoveSelect)[Slot::Arc2].bits == bits::R, "farthest is not R");
}

void stick() {
    const auto l = layout(width, height, mm);
    const auto wide = scene(SceneId::MapIdle), page = scene(SceneId::Page);
    Fingers f;
    // The wide stick centres where the thumb lands, anywhere in the left part.
    const float x = width * .3f, y = height * .5f;
    check(f.down(l, wide, 1, x, y) && f.buttons() == 0, "a stick touch pressed a direction");
    f.move(l, wide, 1, x, y - 6 * mm);
    check(f.buttons() == bits::Up, "drag up");
    f.move(l, wide, 1, x - 30 * mm, y + 2 * mm);
    check(f.buttons() == bits::Left, "drag far left");
    check(f.stick() && f.stick()->cx == x && f.stick()->cy == y, "the stick's centre moved");
    f.up(1);
    // On our pages the stick stays in the corner; the middle of the screen is the page's.
    check(!f.down(l, page, 2, width * .3f, height * .5f), "a page touch taken by the stick");
    check(f.down(l, page, 3, l.rest_x + 8 * mm, l.rest_y), "the corner stick missed");
    check(f.buttons() == bits::Right, "the corner stick's direction is from its fixed centre");
    f.clear();
    // Above the stick area along the top, and right of it, nothing (in a scene without taps).
    check(!f.down(l, wide, 4, width * .5f, height * .5f), "the middle of the map is a control");
    check(!in_stick_area(l, wide, width * .2f, 6 * mm), "the stick area reaches the top bar");
}

void buttons() {
    const auto l = layout(width, height, mm);
    const auto map = scene(SceneId::MapIdle), dialogue = scene(SceneId::Dialogue);
    Fingers f;
    check(f.down(l, map, 1, width * .3f, height * .5f), "stick");
    f.move(l, map, 1, width * .3f + 8 * mm, height * .5f);
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
    check(f.down(l, dialogue, 7, width * .2f, height * .6f) && f.buttons() == 0, "the dialogue's stick area pages");
    check(f.up(7) && !f.up(7) && f.empty(), "release");
}
}

int main() {
    try { places(); scenes(); stick(); buttons(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
