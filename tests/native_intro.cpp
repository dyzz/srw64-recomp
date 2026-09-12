#include "intro_controls.hpp"
#include <cassert>
#include <cstdio>
using srw64::intro::Controls;
int main() {
    Controls c;
    assert(c.input(0x1010)==0x1010); // A title/menu chord is untouched.
    c.scene(true);
    assert(c.input(0x1010)==0x1010 && !c.pending); // Held on entry cannot skip.
    c.input(0);c.input(0x10);
    assert(c.input(0x1010)==0 && c.pending);
    assert(!c.take(false) && c.pending); // Retain request through opening fade.
    assert(c.take(true) && !c.take(true));
    c.scene(false);
    assert(c.input(0x1010)==0); // Suppress the chord through the next menu.
    assert(c.input(0x1000)==0); // Releasing R first cannot expose held Start.
    assert(c.input(0)==0);
    assert(c.input(0x1000)==0x1000); // Fresh menu Start works.
    c.input(0);c.scene(true);
    assert(c.input(0x8000)==0x8000 && !c.pending); // Normal page confirm intact.
    c.input(0x1010);c.scene(false);c.scene(true);
    assert(!c.take(true)); // Overlay/scene changes cancel pending requests.
    std::puts("native intro input boundaries passed");
}
