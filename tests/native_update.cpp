// The update check's version comparison and website language (src/host/update_version.hpp).
#include "../src/host/update_version.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) throw std::runtime_error(what);
}
}

int main() {
    using srw64::update::newer;
    using srw64::update::site_language;
    try {
        check(newer("0.3.6", "0.3.5"), "a later patch is newer");
        check(newer("0.3.10", "0.3.9"), "numbers compare as numbers, not text");
        check(newer("0.4", "0.3.9"), "a shorter version can be newer");
        check(newer("1.0.0", "0.9.9"), "a major version is newer");
        check(!newer("0.3.5", "0.3.5"), "the same version is not newer");
        check(!newer("0.3.5", "0.3.5.0") && !newer("0.3.5.0", "0.3.5"), "a trailing zero changes nothing");
        check(!newer("0.3.4", "0.3.5"), "an older version is not newer");
        check(!newer("0.3.5", "0.3.5-dirty") && newer("0.3.6", "0.3.5-dirty"), "a suffix is ignored");
        check(!newer("", "0.3.5") && !newer("v0.3.6", "0.3.5"), "something that is not a version is never newer");
        check(newer("0.3.6", "?"), "a build with no version takes any release as newer");
        check(site_language("zh-Hans") == "zh" && site_language("ja") == "ja" && site_language("en") == "en" &&
                  site_language("ko") == "en",
              "website languages");
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "native update: " << checks << " checks passed\n";
}
