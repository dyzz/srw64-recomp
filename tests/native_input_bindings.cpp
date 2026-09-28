// Remappable controls (src/host/input_bindings.hpp): make recomp-input-bindings-test.
#include "input_bindings.hpp"
#include <cassert>
#include <cstdio>
#include <set>

using namespace srw64::input;

int main() {
    const auto defaults = default_bindings();
    const auto keys = [&](std::set<int> down) { return key_mask(defaults, [&](int k) { return down.count(k) > 0; }); };
    // The keyboard is PCSX2's layout: each key does what the controller button in its place does.
    assert(keys({scancode::K}) == 0x8000);                 // south: A
    assert(keys({scancode::L}) == 0x4000);                 // east: B
    assert(keys({scancode::J}) == 0x0002);                 // west: C-left (the faster map cursor)
    assert(keys({scancode::I}) == pad_animation);          // north: the battle animation
    assert(keys({scancode::Return}) == 0x1000 && keys({scancode::Backspace}) == pad_view);
    assert(keys({scancode::Q, scancode::E}) == 0x0030);
    assert(keys({scancode::N1, scancode::N3}) == (pad_l2 | pad_r2));
    assert(keys({scancode::N2}) == pad_language && keys({scancode::N4}) == pad_images);
    assert(keys({scancode::T, scancode::G, scancode::F, scancode::H}) == 0x000F);
    assert(keys({scancode::Up, scancode::Down, scancode::Left, scancode::Right}) == 0x0F00);
    assert(keys({scancode::W, scancode::S, scancode::A, scancode::D}) == 0xF0000u);
    assert(keys({scancode::Z}) == 0 && keys({scancode::X}) == 0 && keys({scancode::Space}) == 0 && keys({scancode::Escape}) == 0);
    // The keyboard binds what the controller binds, Z on neither.
    for (size_t i = 0; i < action_count; ++i) assert(defaults.keys[i].empty() == defaults.pads[i].empty());
    // The classic layout, which the debug interface's keys keep.
    const auto classic = classic_keys();
    const auto old = [&](std::set<int> down) { return key_mask(classic, [&](int k) { return down.count(k) > 0; }); };
    assert(old({scancode::Z}) == 0x8000 && old({scancode::X}) == 0x4000 && old({scancode::Space}) == 0x2000);
    assert(old({scancode::I, scancode::K, scancode::J, scancode::L}) == 0x000F);
    const auto pads = [&](std::set<uint8_t> buttons, std::array<int, pad_axis::Count> axes) {
        return pad_mask(defaults, [&](uint8_t b) { return buttons.count(b) > 0; }, [&](uint8_t a) { return axes[a]; });
    };
    assert(pads({pad_button::A}, {}) == 0x8000);
    assert(pads({pad_button::B}, {}) == 0x4000);
    // The Deck's freed buttons (docs/gameplay/original-controls.md): no Z; X holds C-left
    // (the faster map cursor), Y the battle animation, L3 / R3 language and Original / HD.
    assert(pads({pad_button::X}, {}) == 0x0002);
    assert(pads({pad_button::Y}, {}) == pad_animation);
    assert(pads({pad_button::LeftStick}, {}) == pad_language && pads({pad_button::RightStick}, {}) == pad_images);
    assert(defaults.pads[size_t(Action::Z)].empty() && defaults.keys[size_t(Action::Z)].empty());
    assert(pads({pad_button::Back}, {}) == pad_view);
    assert(pads({}, {0, 0, 0, -20000, 0, 0}) == 0x0008);      // right stick up: C-up
    assert(pads({}, {0, 0, 0, -16000, 0, 0}) == 0);           // not past the threshold
    assert(pads({}, {0, 0, 0, 0, 30000, 30000}) == (pad_l2 | pad_r2));
    assert(pads({}, {-20000, 20000, 0, 0, 0, 0}) == ((1u << 18) | (1u << 17)));  // stick left + down

    // A new key replaces the action's keys; the action that held it takes the old one.
    auto b = defaults;
    assign_key(b, Action::A, scancode::G);
    assert(b.keys[size_t(Action::A)] == std::vector<int>{scancode::G});
    assert(b.keys[size_t(Action::CDown)] == std::vector<int>{scancode::K});
    // An unused key takes nothing from anyone.
    assign_key(b, Action::Z, scancode::Space);
    assert(b.keys[size_t(Action::Z)] == std::vector<int>{scancode::Space});
    assert(b.keys[size_t(Action::A)] == std::vector<int>{scancode::G});
    // C-left keeps F when J goes elsewhere.
    assign_key(b, Action::B, scancode::J);
    assert(b.keys[size_t(Action::CLeft)] == std::vector<int>{scancode::F});
    // C-left keeps its stick direction when X goes elsewhere, so nothing is swapped in.
    b = defaults;
    assign_pad(b, Action::Z, button(pad_button::X));
    assert(b.pads[size_t(Action::CLeft)] == std::vector<PadInput>{axis(pad_axis::RightX, -1)});
    assert(b.pads[size_t(Action::Z)] == std::vector<PadInput>{button(pad_button::X)});
    // An action with no inputs gives nothing back: Y moves to Z, the animation keeps nothing.
    b = defaults;
    assign_pad(b, Action::Z, button(pad_button::Y));
    assert(b.pads[size_t(Action::Animation)].empty());
    // A trigger swapped onto the settings button: View moves to the trigger's action.
    b = defaults;
    assign_pad(b, Action::Settings, axis(pad_axis::TriggerLeft, 1));
    assert(b.pads[size_t(Action::AuxLeft)] == std::vector<PadInput>{button(pad_button::Back)});
    // Assigning the input an action already has changes nothing.
    b = defaults;
    assign_pad(b, Action::A, button(pad_button::A));
    assert(b == defaults);
    // Clearing leaves the action without inputs of that device.
    clear_keys(b, Action::A);
    assert(b.keys[size_t(Action::A)].empty() && !(b == defaults));

    // The live copy counts changes and calls the saver only for real ones.
    static int saved = 0;
    auto& live = live_bindings();
    live.set_saver([](const Bindings&) { ++saved; });
    const auto before = live.revision();
    live.set(defaults);
    assert(saved == 0 && live.revision() == before);
    live.set(b);
    assert(saved == 1 && live.revision() == before + 1 && live.get() == b);
    live.set(defaults, false);
    assert(saved == 1 && live.get() == defaults);
    assert(action_of_key(defaults, scancode::K) && *action_of_key(defaults, scancode::K) == Action::A);
    assert(!action_of_key(defaults, scancode::Escape) && !action_of_key(defaults, scancode::Z));
    std::puts("ok");
}
