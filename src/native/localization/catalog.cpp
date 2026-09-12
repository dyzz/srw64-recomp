#include "catalog.hpp"
#include <cstdio>
#include <stdexcept>
#include <atomic>

namespace srw64::localization {
TextKey TextKey::base(uint16_t table,uint16_t id) {
    if(table>99)throw std::runtime_error("Text table outside TextKey range");
    char text[32];std::snprintf(text,sizeof(text),"base:t%02u_%05u",table,id);
    return {text};
}
void Catalog::load(const nlohmann::json& data) {
    translated.clear();source.clear();labels.clear();
    const bool legacy=data.at("schema")=="srw64.native-dialogue-data.v1";
    if(!legacy && data.at("schema")!="srw64.native-dialogue-data.v2")throw std::runtime_error("Unsupported text catalog");
    locale=data.at("config").value("locale","zh-Hans");
    font=data.at("config").at("font").get<std::string>();
    revision=data.value("catalog_sha256","legacy");
    for(const auto& [key,value]:data.at("entries").items())
        translated.emplace(legacy?TextKey::base(0,std::stoul(key)).value:key,value.get<std::string>());
    if(!legacy) {
        source=data.at("source_entries").get<decltype(source)>();
        labels=data.at("ui").get<decltype(labels)>();
    } else {
        labels={{"manual","手动"},{"auto","自动"},{"fast","快速剧情"},{"skip","跳过剧情…"},
            {"font_size","字号"},{"controls","↑↓自动速度  Z下一页  Q回看  I/K字号  E+Z快进  E+Enter跳过"},
            {"history_title","对话回看"},{"history_controls","↑↓滚动 · Q / Z / X 返回"}};
    }
}
const std::string* Catalog::resolve(const TextKey& key) const {
    auto found=translated.find(key.value);
    if(found!=translated.end())return &found->second;
    found=source.find(key.value);
    return found==source.end()?nullptr:&found->second;
}
std::string Catalog::ui(const std::string& key) const {
    auto found=labels.find(key);return found==labels.end()?key:found->second;
}
namespace {
std::map<std::string,Snapshot> catalogs;
Snapshot active=std::make_shared<const Catalog>();
thread_local Snapshot scoped;
}
Snapshot snapshot(){return scoped?scoped:std::atomic_load(&active);}
const Catalog& catalog() {
    // Registered values live for the entire run. Retain a thread-local pin for
    // the default/uninitialized test catalog as well.
    thread_local Snapshot pin;pin=snapshot();return *pin;
}
Snapshot find(const std::string& locale){auto it=catalogs.find(locale);return it==catalogs.end()?nullptr:it->second;}
const std::map<std::string,Snapshot>& registered(){return catalogs;}
void activate(Snapshot value){if(!value)throw std::runtime_error("Unknown locale");std::atomic_store(&active,std::move(value));}
void initialize(const nlohmann::json& data) {
    std::map<std::string,Snapshot> next;
    if(data.contains("locale_catalogs")) {
        auto base=data;base.erase("locale_catalogs");
        for(const auto& [locale,row]:data.at("locale_catalogs").items()) {
            auto input=base;for(const auto& [key,value]:row.items())input[key]=value;
            auto value=std::make_shared<Catalog>();value->load(input);
            if(value->locale!=locale)throw std::runtime_error("Locale registry identity mismatch");
            next.emplace(locale,std::move(value));
        }
    } else {
        auto value=std::make_shared<Catalog>();value->load(data);next.emplace(value->locale,std::move(value));
    }
    const auto selected=data.at("config").value("locale","zh-Hans");
    if(!next.contains(selected))throw std::runtime_error("Selected locale is unavailable");
    catalogs=std::move(next);activate(catalogs.at(selected));
}
Scope::Scope(Snapshot value):previous(scoped){if(value)scoped=std::move(value);}
Scope::~Scope(){scoped=std::move(previous);}
}
