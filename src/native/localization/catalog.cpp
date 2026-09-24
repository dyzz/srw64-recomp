#include "catalog.hpp"
#include <cstdio>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <vector>

namespace srw64::localization {
TextKey TextKey::base(uint16_t table,uint16_t id) {
    if(table>99)throw std::runtime_error("Text table outside TextKey range");
    char text[32];std::snprintf(text,sizeof(text),"base:t%02u_%05u",table,id);
    return {text};
}
void Catalog::load(const nlohmann::json& data) {
    translated.clear();source.clear();labels.clear();
    if(data.at("schema")!="srw64.native-dialogue-data.v2")throw std::runtime_error("Unsupported text catalog");
    locale=data.at("config").value("locale","zh-Hans");
    font=data.at("config").at("font").get<std::string>();
    revision=data.value("catalog_sha256","legacy");
    for(const auto& [key,value]:data.at("entries").items())translated.emplace(key,value.get<std::string>());
    source=data.at("source_entries").get<decltype(source)>();
    labels=data.at("ui").get<decltype(labels)>();
}
const std::string* Catalog::resolve(const TextKey& key) const {
    auto found=translated.find(key.value);
    if(found!=translated.end())return &found->second;
    found=source.find(key.value);
    return found==source.end()?nullptr:&found->second;
}
const std::string* Catalog::source_text(const TextKey& key) const {
    const auto found=source.find(key.value);
    return found==source.end()?nullptr:&found->second;
}
std::shared_ptr<Catalog> Catalog::with_translations(const std::map<std::string,std::string>& entries,const std::string& layer) const {
    auto copy=std::make_shared<Catalog>(*this);
    for(const auto& [key,value]:entries)copy->translated[key]=value;
    if(!entries.empty())copy->revision=revision+"+"+layer;
    return copy;
}
std::string Catalog::ui(const std::string& key) const {
    auto found=labels.find(key);return found==labels.end()?key:found->second;
}
namespace {
std::mutex registry;
std::map<std::string,Snapshot> catalogs;
std::vector<std::string> locale_order;
std::map<std::string,std::string> locale_names;
Snapshot active=std::make_shared<const Catalog>();
thread_local Snapshot scoped;
}
Snapshot snapshot(){return scoped?scoped:std::atomic_load(&active);}
const Catalog& catalog() {
    // Registered values live for the entire run. Retain a thread-local pin for
    // the default/uninitialized test catalog as well.
    thread_local Snapshot pin;pin=snapshot();return *pin;
}
Snapshot find(const std::string& locale){std::lock_guard lock(registry);auto it=catalogs.find(locale);return it==catalogs.end()?nullptr:it->second;}
std::map<std::string,Snapshot> registered(){std::lock_guard lock(registry);return catalogs;}
void replace(std::map<std::string,Snapshot> value) {
    std::lock_guard lock(registry);
    if(value.size()!=catalogs.size())throw std::runtime_error("Replacement changes the locale registry");
    for(const auto& [locale,catalog]:value)
        if(!catalogs.contains(locale) || !catalog || catalog->locale!=locale)throw std::runtime_error("Replacement changes the locale registry");
    catalogs=std::move(value);
}
std::string next_locale(const std::string& current) {
    auto it=std::find(locale_order.begin(),locale_order.end(),current);
    if(it==locale_order.end())throw std::runtime_error("Current locale is unavailable");
    return ++it==locale_order.end()?locale_order.front():*it;
}
std::string display_name(const std::string& locale) {
    auto it=locale_names.find(locale);return it==locale_names.end()?locale:it->second;
}
std::string language_choices() {
    std::string result;
    for(const auto& locale:locale_order){if(!result.empty())result+=" / ";result+=display_name(locale);}
    return result;
}
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
    std::vector<std::string> order;
    std::map<std::string,std::string> names;
    if(data.contains("locale_options")) {
        for(const auto& option:data.at("locale_options")) {
            const auto locale=option.at("locale").get<std::string>();
            if(!next.contains(locale) || names.contains(locale))throw std::runtime_error("Invalid locale cycle");
            order.push_back(locale);names.emplace(locale,option.value("label",locale));
        }
        if(order.size()!=next.size())throw std::runtime_error("Incomplete locale cycle");
    } else {
        for(const auto& [locale,value]:next){order.push_back(locale);names.emplace(locale,locale);}
    }
    {std::lock_guard lock(registry);catalogs=std::move(next);}
    locale_order=std::move(order);locale_names=std::move(names);
    activate(find(selected));
}
Scope::Scope(Snapshot value):previous(scoped){if(value)scoped=std::move(value);}
Scope::~Scope(){scoped=std::move(previous);}
}
