#pragma once
// Which built-in language a language package's locale stands on (docs/design/mod-language-packs.md),
// for the readers of per-language texts that know no catalog (campaign_library.hpp and the
// like, tested without one). catalog.cpp's add_locale() fills it in.
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace srw64::localization {
struct PackageLocales {
    std::mutex mutex;
    std::map<std::string,std::string> fallback;   // a package's locale -> its built-in fallback
};
inline PackageLocales& package_locales(){static PackageLocales value;return value;}
// The built-in locale whose typesetting a locale follows: itself, or a package's fallback.
inline std::string style(const std::string& locale) {
    auto& p=package_locales();
    std::lock_guard lock(p.mutex);
    const auto found=p.fallback.find(locale);
    return found==p.fallback.end()?locale:found->second;
}
// Where a text written per language is looked for, in order, ending with ja: zh-Hant then
// zh-Hans; a package's locale then its fallback's chain.
inline std::vector<std::string> fallbacks(const std::string& locale) {
    std::vector<std::string> chain{locale};
    const auto built_in=style(locale);
    if(built_in!=locale)chain.push_back(built_in);
    if(built_in=="zh-Hant")chain.push_back("zh-Hans");
    if(chain.back()!="ja")chain.push_back("ja");
    return chain;
}
// A {locale: text} object (or a plain string) in a locale by fallbacks(), then any text it has.
template<class Json> std::string pick(const Json& field,const std::string& locale) {
    if(field.is_string())return field.template get<std::string>();
    if(!field.is_object())return {};
    for(const auto& key:fallbacks(locale))
        if(field.contains(key) && field.at(key).is_string())return field.at(key).template get<std::string>();
    for(const auto& [key,value]:field.items())if(value.is_string())return value.template get<std::string>();
    return {};
}
}
