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
    std::string ui(const std::string& key) const;
};
using Snapshot=std::shared_ptr<const Catalog>;
const Catalog& catalog();
Snapshot snapshot();
Snapshot find(const std::string& locale);
const std::map<std::string,Snapshot>& registered();
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
