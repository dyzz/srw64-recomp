// The host's tagged draws as quads (native_quads.hpp): the providers' list, in every build,
// so the modules register whether a renderer asks or not.
#include "native_quads.hpp"

#include <algorithm>
#include <mutex>

namespace srw64::native_quads {
namespace {
std::mutex providers_mutex;
std::vector<Provider> providers;
}
void add(Provider provider) {
    std::lock_guard lock(providers_mutex);
    if (std::find(providers.begin(), providers.end(), provider) == providers.end()) providers.push_back(provider);
}
bool find(uint32_t id, std::vector<Quad>& out) {
    std::lock_guard lock(providers_mutex);
    for (const auto provider : providers) if (provider(id, out)) return true;
    return false;
}
}  // namespace srw64::native_quads
