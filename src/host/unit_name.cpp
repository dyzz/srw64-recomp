#include "unit_name.hpp"
#include "game_hooks.hpp"
#include "native_name_entry.hpp"
#include "localization/catalog.hpp"
#include "json/json.hpp"
#include <fstream>
#include <mutex>

uint64_t srw64_current_vi();
namespace srw64::unit_name {
namespace {
std::mutex mutex;
std::ofstream log;
void record(nlohmann::json row) {
    std::lock_guard lock(mutex);
    if(!log.is_open())return;
    row["schema"]="srw64.unit-name-event.v1";row["vi"]=srw64_current_vi();
    log<<row.dump()<<'\n';log.flush();
}
}
void configure(const std::filesystem::path& output) {
    if(!output.empty())log.open(output/"unit-name-events.jsonl");
    // Registered whether or not the name pages are native: dialogue shows the name too.
    std::map<std::string,std::string> by_locale;
    for(const auto& [locale,catalog]:localization::registered())if(catalog)by_locale[locale]=catalog->ui("unit_default_name");
    if(add_default(names::default_names(),by_locale))record({{"kind","default"},{"names",by_locale}});
    srw64_game_hooks.choice_step=[](uint8_t* ram,uint32_t vm) {
        const auto text=guest::read(ram,guest::read(ram,vm+0x1C,4)+4,2);
        if(!answer_choice(ram,vm))return false;
        record({{"kind","choice-answered"},{"text",text},{"answer",1}});
        return true;
    };
    srw64_game_hooks.unit_name_page=[](uint8_t*) {
        record({{"kind","page-skipped"},{"command","3D5E"}});
        return true;
    };
}
}
