#pragma once
#include <map>
#include <string>
#include <cstdint>
#include <memory>
#include "json/json.hpp"

namespace srw64::localization {
struct TextKey {
    std::string value;
    static TextKey base(uint16_t table, uint16_t id);
};
// All registered catalogs are immutable after initialization. A frame pins the
// catalog used to lay it out, so in-flight old frames keep their old font/UI.
class Catalog {
    std::map<std::string,std::string> translated, source, labels;
public:
    std::string locale="zh-Hans", font="PingFangSC-Medium", revision="legacy";
    void load(const nlohmann::json& data);
    const std::string* resolve(const TextKey& key) const;
    // The original record, whatever this catalog translates it to.
    const std::string* source_text(const TextKey& key) const;
    // This catalog with `entries` replacing or adding translations (dialogue text files).
    std::shared_ptr<Catalog> with_translations(const std::map<std::string,std::string>& entries,const std::string& layer) const;
    std::string ui(const std::string& key) const;
    const std::map<std::string,std::string>& ui_labels() const { return labels; }
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
void initialize(const nlohmann::json& data);
void activate(Snapshot value);
class Scope {
    Snapshot previous;
public:
    explicit Scope(Snapshot value);
    ~Scope();
    Scope(const Scope&)=delete;
};
}
