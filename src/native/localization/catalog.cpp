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
namespace {
const std::shared_ptr<const Catalog::Table>& empty_table() {
    static const auto empty=std::make_shared<const Catalog::Table>();
    return empty;
}
const std::string* lookup(const Catalog::Table& table,const std::string& key) {
    const auto found=table.find(key);
    return found==table.end()?nullptr:&found->second;
}
}
Catalog::Catalog():translated(empty_table()),source(empty_table()),labels(empty_table()),layered(empty_table()) {}
void Catalog::load(const nlohmann::json& data) {
    load(data,nlohmann::json::object(),std::make_shared<const Table>(data.at("source_entries").get<Table>()));
}
void Catalog::load(const nlohmann::json& data,const nlohmann::json& row,std::shared_ptr<const Table> source_records,
                   std::shared_ptr<const Table> translations) {
    // A key of the locale's row wins over the same key of the data.
    const auto field=[&](const char* key)->const nlohmann::json& {return row.contains(key)?row.at(key):data.at(key);};
    if(data.at("schema")!="srw64.native-dialogue-data.v2")throw std::runtime_error("Unsupported text catalog");
    const auto& config=field("config");
    locale=config.value("locale","zh-Hans");
    font=config.at("font").get<std::string>();
    revision=row.contains("catalog_sha256")?row.at("catalog_sha256").get<std::string>():data.value("catalog_sha256","legacy");
    if(translations) {
        translated=std::move(translations);
    } else {
        auto entries=std::make_shared<Table>();
        for(const auto& [key,value]:field("entries").items())entries->emplace(key,value.get<std::string>());
        translated=std::move(entries);
    }
    source=std::move(source_records);
    labels=std::make_shared<const Table>(field("ui").get<Table>());
    layered=empty_table();
}
const std::string* Catalog::resolve(const TextKey& key) const {
    for(const auto* table:{layered.get(),translated.get(),source.get()})
        if(const auto* text=lookup(*table,key.value))return text;
    return nullptr;
}
const std::string* Catalog::source_text(const TextKey& key) const {
    return lookup(*source,key.value);
}
std::shared_ptr<Catalog> Catalog::with_translations(Table entries,const std::string& layer) const {
    auto copy=std::make_shared<Catalog>(*this);
    if(entries.empty())return copy;
    // Over this catalog's own layer, if it has one; the tables below stay shared.
    for(const auto& [key,value]:*layered)entries.try_emplace(key,value);
    copy->layered=std::make_shared<const Table>(std::move(entries));
    copy->revision=revision+"+"+layer;
    return copy;
}
std::string Catalog::ui(const std::string& key) const {
    const auto* text=lookup(*labels,key);return text?*text:key;
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
void initialize(const nlohmann::json& data,const string_tables::Tables* tables) {
    std::map<std::string,Snapshot> next;
    const auto table=[&](const std::string& name)->std::shared_ptr<const Catalog::Table> {
        if(!tables)return nullptr;
        const auto found=tables->find(name);
        if(found==tables->end())throw std::runtime_error("String tables lack "+name);
        return found->second;
    };
    if(data.contains("locale_catalogs")) {
        // The original records once, for every locale (a row may have its own).
        const auto shared=tables?table("source"):std::make_shared<const Catalog::Table>(data.at("source_entries").get<Catalog::Table>());
        for(const auto& [locale,row]:data.at("locale_catalogs").items()) {
            auto value=std::make_shared<Catalog>();
            value->load(data,row,row.contains("source_entries")?std::make_shared<const Catalog::Table>(row.at("source_entries").get<Catalog::Table>()):shared,
                        table("entries/"+locale));
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
