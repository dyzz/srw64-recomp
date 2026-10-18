#pragma once
#include <map>
#include <string>
#include <cstdint>
#include <memory>
#include "json/json.hpp"
#include "localization/string_tables.hpp"

namespace srw64::localization {
struct TextKey {
    std::string value;
    static TextKey base(uint16_t table, uint16_t id);
};
// All registered catalogs are immutable after initialization. A frame pins the
// catalog used to lay it out, so in-flight old frames keep their old font/UI.
class Catalog {
public:
    using Table=std::map<std::string,std::string>;
private:
    // Shared, never changed once loaded: every locale's catalog holds the same original
    // records (about 51,000), and a layered copy shares its base's tables. The PS Vita
    // had six copies of each in memory.
    std::shared_ptr<const Table> translated, source, labels, layered;
public:
    std::string locale="zh-Hans", font="PingFangSC-Medium", revision="legacy";
    Catalog();
    void load(const nlohmann::json& data);
    // A locale's row of locale_catalogs over the rest of the data, with the original
    // records already read from it, and its translations when they come apart from the JSON
    // (string_tables.hpp).
    void load(const nlohmann::json& data,const nlohmann::json& row,std::shared_ptr<const Table> source_records,
              std::shared_ptr<const Table> translations=nullptr);
    const std::string* resolve(const TextKey& key) const;
    // The original record, whatever this catalog translates it to.
    const std::string* source_text(const TextKey& key) const;
    // This catalog with `entries` replacing or adding translations (dialogue text files).
    std::shared_ptr<Catalog> with_translations(Table entries,const std::string& layer) const;
    std::string ui(const std::string& key) const;
    const Table& ui_labels() const { return *labels; }
};
using Snapshot=std::shared_ptr<const Catalog>;
const Catalog& catalog();
Snapshot snapshot();
Snapshot find(const std::string& locale);
// A copy: dialogue text reloads replace the registered catalogs as a whole.
std::map<std::string,Snapshot> registered();
// Swap in new catalogs for the same locales; the active one stays until activate().
void replace(std::map<std::string,Snapshot> value);
// Cycles in the profile's locale_options order, independent of map sorting.
std::string next_locale(const std::string& current);
std::string display_name(const std::string& locale);
std::string language_choices();
// With `tables` (prepared data, string_tables.hpp), the original records and the
// translations come from them instead of the JSON.
void initialize(const nlohmann::json& data,const string_tables::Tables* tables=nullptr);
void activate(Snapshot value);
class Scope {
    Snapshot previous;
public:
    explicit Scope(Snapshot value);
    ~Scope();
    Scope(const Scope&)=delete;
};
}
