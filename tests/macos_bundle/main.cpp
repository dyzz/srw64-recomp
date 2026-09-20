#include "app/desktop.hpp"
#include <iostream>
extern "C" int srw64_bundle_fixture();
int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--help") return 2;
    const auto ui = srw64::app::macos_desktop_ui();
    if (!ui.choose_rom || !ui.show_error || srw64_bundle_fixture() != 42) return 3;
    // Do not open modal dialogs on a CI worker. Cocoa backend compilation is
    // distinct from interactive picker/SDL/game lifecycle acceptance.
    std::cout << "SRW64_BUNDLE_SMOKE_OK\n";
    return 0;
}
