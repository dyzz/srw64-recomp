// The on-screen controller's layout and fingers (src/host/touch_pad.hpp).
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
float cx(const Shape& s) { return s.round ? s.x : s.x + s.w / 2; }
float cy(const Shape& s) { return s.round ? s.y : s.y + s.h / 2; }

void shapes() {
    const auto l = layout(width, height, mm);
    for (const auto& s : l.shapes) {
        const float left = s.round ? s.x - s.w : s.x, right = s.round ? s.x + s.w : s.x + s.w;
        const float top = s.round ? s.y - s.w : s.y, bottom = s.round ? s.y + s.w : s.y + s.h;
        check(left >= 0 && right <= width && top >= 0 && bottom <= height, "a control leaves the screen");
        check(hit(l, cx(s), cy(s)) == s.control, "a control's centre does not hit it");
    }
    // Hit areas never overlap: each control's centre hits only itself above, and the
    // corners' neighbours are apart.
    check(l[Control::L2].y < l[Control::L1].y && l[Control::R2].y < l[Control::R1].y, "the 2 buttons are not above the 1 buttons");
    check(l[Control::DPad].x < width / 2 && l[Control::A].x > width / 2, "D-pad and A on the wrong sides");
    check(cy(l[Control::Option]) > height * .9f && cx(l[Control::Option]) < width / 2 && cx(l[Control::Start]) > width / 2,
          "OPTION and START are not at the bottom centre");
    check(!hit(l, width / 2, height / 2), "the middle of the picture is a control");
    // Neighbours stay apart: L1 above the D-pad, R1 above A, B beside START.
    const auto& pad = l[Control::DPad];
    check(pad.y - pad.w - (l[Control::L1].y + l[Control::L1].h) >= 5 * mm, "the D-pad crowds L1");
    check(l[Control::A].y - l[Control::A].w - (l[Control::R1].y + l[Control::R1].h) >= 5 * mm, "A crowds R1");
    check(l[Control::B].x - l[Control::B].w - (l[Control::Start].x + l[Control::Start].w) >= 5 * mm, "B crowds START");
    check(l[Control::A].w >= 6 * mm && l[Control::DPad].w >= 11 * mm, "controls smaller than a thumb");
}

void dpad() {
    const auto l = layout(width, height, mm);
    const auto& p = l[Control::DPad];
    check(direction(l, p.x, p.y) == 0, "the D-pad's centre presses a direction");
    check(direction(l, p.x, p.y - 8 * mm) == bits::Up, "up");
    check(direction(l, p.x, p.y + 8 * mm) == bits::Down, "down");
    check(direction(l, p.x - 8 * mm, p.y) == bits::Left, "left");
    check(direction(l, p.x + 8 * mm, p.y + 3 * mm) == bits::Right, "right, a little low");
}

void fingers() {
    const auto l = layout(width, height, mm);
    const auto& p = l[Control::DPad];
    Fingers f;
    check(!f.down(l, 1, width / 2, height / 2), "a finger on the picture taken");
    check(f.down(l, 2, p.x + 8 * mm, p.y), "D-pad finger not taken");
    check(f.down(l, 3, l[Control::A].x, l[Control::A].y), "A finger not taken");
    check(f.buttons() == (bits::Right | bits::A), "right and A together");
    // The thumb slides on the D-pad, and past its edge it keeps the direction.
    f.move(l, 2, p.x, p.y - 20 * mm);
    check(f.buttons() == (bits::Up | bits::A), "D-pad slide");
    // A slides to B; then off every control, still B.
    f.move(l, 3, l[Control::B].x, l[Control::B].y);
    check(f.buttons() == (bits::Up | bits::B), "slide from A to B");
    f.move(l, 3, width / 2, height / 2);
    check(f.buttons() == (bits::Up | bits::B), "B lost off the controls");
    check(f.pressed(Control::B) && !f.pressed(Control::A), "pressed controls");
    check(f.up(3) && !f.up(3), "finger release");
    check(f.buttons() == bits::Up, "after B lifted");
    // The host's buttons.
    check(f.down(l, 4, cx(l[Control::R2]), cy(l[Control::R2])) && (f.buttons() & srw64::input::pad_r2), "R2");
    check(f.down(l, 5, cx(l[Control::Option]), cy(l[Control::Option])) && (f.buttons() & srw64::input::pad_view), "OPTION");
    check(f.down(l, 6, cx(l[Control::L1]), cy(l[Control::L1])) && (f.buttons() & bits::L), "L1");
    f.clear();
    check(f.empty() && f.buttons() == 0, "clear");
}
}

int main() {
    try { shapes(); dpad(); fingers(); std::cout << checks << " checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
