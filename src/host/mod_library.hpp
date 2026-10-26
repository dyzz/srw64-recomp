#pragma once
// MOD packages (docs/design/mod-packages.md §2–§3): the manifest, version requirements,
// dependencies and the load order. A package is a folder <mods>/<id>/ holding mod.json
// and the content folders; a .srw64mod file is that folder zipped (mod_library.cpp
// installs it). This header reads nothing from disk except through `summarize`, so the
// rules are tested without a game (tests/native_mod_library.cpp).
#include "campaign_library.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace srw64::mods {
namespace fs=std::filesystem;
using json=nlohmann::json;

inline constexpr const char* manifest_schema="srw64.mod.v1";
inline constexpr const char* state_schema="srw64.mods-state.v1";
inline constexpr const char* package_extension=".srw64mod";
// The licences a package may carry (docs/design/mod-packages.md §13.4): ones that allow
// the package to be passed on. The field is optional here; the submission check
// (tools/mods/validate.py) requires it.
inline constexpr const char* licenses[]={"CC-BY-4.0","CC-BY-SA-4.0","CC-BY-NC-4.0","CC-BY-NC-SA-4.0","CC0-1.0","MIT","GPL-3.0-or-later"};
inline bool valid_license(const std::string& text){return std::find(std::begin(licenses),std::end(licenses),text)!=std::end(licenses);}

// A version: up to three numbers, "1", "1.2" or "1.2.3" (missing parts are 0).
struct Version {
    int major{},minor{},patch{};
    auto operator<=>(const Version&) const=default;
};
inline std::optional<Version> parse_version(const std::string& text) {
    Version v;int* parts[]={&v.major,&v.minor,&v.patch};
    size_t at=0;unsigned n=0;
    while(n<3) {
        size_t end=at;
        while(end<text.size() && text[end]>='0' && text[end]<='9')++end;
        if(end==at || end-at>6)return std::nullopt;
        *parts[n++]=std::stoi(text.substr(at,end-at));
        if(end==text.size())return v;
        if(text[end]!='.')return std::nullopt;
        at=end+1;
    }
    return std::nullopt;
}
inline std::string version_text(const Version& v) {
    return std::to_string(v.major)+"."+std::to_string(v.minor)+"."+std::to_string(v.patch);
}
// The leading x.y.z of a release name such as "0.4.9" or "0.4.9-dirty".
inline std::optional<Version> leading_version(const std::string& text) {
    size_t end=0;
    while(end<text.size() && ((text[end]>='0' && text[end]<='9') || text[end]=='.'))++end;
    while(end>0 && text[end-1]=='.')--end;
    return parse_version(text.substr(0,end));
}

// A requirement: "*" or space-separated clauses, each an operator (>=, >, <=, <, =)
// and a version, all of which must hold: ">=1.0 <2".
inline bool valid_requirement(const std::string& text) {
    if(text=="*")return true;
    bool any=false;
    for(size_t at=0;at<text.size();) {
        while(at<text.size() && text[at]==' ')++at;
        if(at==text.size())break;
        size_t end=text.find(' ',at);
        std::string clause=text.substr(at,end==std::string::npos?std::string::npos:end-at);
        size_t op=0;
        while(op<clause.size() && (clause[op]=='>' || clause[op]=='<' || clause[op]=='='))++op;
        const auto oper=clause.substr(0,op);
        if(!(oper==">=" || oper==">" || oper=="<=" || oper=="<" || oper=="=") || !parse_version(clause.substr(op)))return false;
        any=true;
        if(end==std::string::npos)break;
        at=end;
    }
    return any;
}
inline bool satisfies(const Version& v,const std::string& requirement) {
    if(requirement=="*")return true;
    for(size_t at=0;at<requirement.size();) {
        while(at<requirement.size() && requirement[at]==' ')++at;
        if(at==requirement.size())break;
        size_t end=requirement.find(' ',at);
        const std::string clause=requirement.substr(at,end==std::string::npos?std::string::npos:end-at);
        size_t op=0;
        while(op<clause.size() && (clause[op]=='>' || clause[op]=='<' || clause[op]=='='))++op;
        const auto oper=clause.substr(0,op);
        const auto want=parse_version(clause.substr(op));
        if(!want)return false;
        const bool ok=oper==">="?v>=*want:oper==">"?v>*want:oper=="<="?v<=*want:oper=="<"?v<*want:v==*want;
        if(!ok)return false;
        if(end==std::string::npos)break;
        at=end;
    }
    return true;
}

// The content a package carries, counted for the manager's list.
struct Contents {
    unsigned music{},voices{},art{},dialogue{},data{};
    bool campaign{},challenge{},story{},rules{};
    bool empty() const {return !music && !voices && !art && !dialogue && !data && !campaign && !challenge && !story && !rules;}
};

// A problem found while reading or resolving a package: a code the UI words (mod_error_*)
// and what fills it in.
struct Problem {
    std::string code,detail;
    bool operator==(const Problem&) const=default;
};

struct Manifest {
    std::string id;
    Version version;
    json name,description;              // a string or {locale: string}
    std::vector<std::string> authors;
    std::string license;                // one of `licenses`, or empty
    std::string game="*";
    std::map<std::string,std::string> needs,follows,conflicts;   // "requires", "optional", "conflicts"
};

// mod.json, strictly: unknown fields are errors (docs/design/mod-packages.md §2).
inline std::variant<Manifest,Problem> parse_manifest(const json& data) {
    if(!data.is_object())return Problem{"manifest","not an object"};
    static const std::set<std::string> known{"schema","id","version","name","description","authors","license","game","requires","optional","conflicts"};
    for(const auto& [key,value]:data.items())if(!known.contains(key))return Problem{"field",key};
    if(data.value("schema","")!=manifest_schema)return Problem{"schema",data.contains("schema") && data["schema"].is_string()?data["schema"].get<std::string>():""};
    Manifest m;
    if(!data.contains("id") || !data["id"].is_string() || !campaign_library::valid_id(data["id"].get<std::string>()))return Problem{"id",""};
    m.id=data["id"].get<std::string>();
    if(!data.contains("version") || !data["version"].is_string())return Problem{"version",""};
    const auto version=parse_version(data["version"].get<std::string>());
    if(!version)return Problem{"version",data["version"].get<std::string>()};
    m.version=*version;
    const auto text_field=[](const json& v){
        if(v.is_string())return !v.get<std::string>().empty();
        if(!v.is_object() || v.empty())return false;
        for(const auto& [k,t]:v.items())if(!t.is_string())return false;
        return true;
    };
    if(!data.contains("name") || !text_field(data["name"]))return Problem{"name",""};
    m.name=data["name"];
    if(data.contains("description")) {
        if(!text_field(data["description"]))return Problem{"description",""};
        m.description=data["description"];
    }
    if(data.contains("authors")) {
        if(!data["authors"].is_array())return Problem{"authors",""};
        for(const auto& a:data["authors"]) {
            if(!a.is_string())return Problem{"authors",""};
            m.authors.push_back(a.get<std::string>());
        }
    }
    if(data.contains("license")) {
        if(!data["license"].is_string() || !valid_license(data["license"].get<std::string>()))return Problem{"license",data["license"].is_string()?data["license"].get<std::string>():""};
        m.license=data["license"].get<std::string>();
    }
    if(data.contains("game")) {
        if(!data["game"].is_string() || !valid_requirement(data["game"].get<std::string>()))return Problem{"game",""};
        m.game=data["game"].get<std::string>();
    }
    for(const auto& [key,target]:{std::pair<const char*,std::map<std::string,std::string>*>{"requires",&m.needs},{"optional",&m.follows},{"conflicts",&m.conflicts}}) {
        if(!data.contains(key))continue;
        if(!data[key].is_object())return Problem{key,""};
        for(const auto& [id,want]:data[key].items()) {
            if(!campaign_library::valid_id(id) || id==m.id || !want.is_string() || !valid_requirement(want.get<std::string>()))return Problem{key,id};
            (*target)[id]=want.get<std::string>();
        }
    }
    return m;
}

// One installed package as the manager shows it.
struct Package {
    fs::path folder;
    std::optional<Manifest> manifest;   // empty when mod.json is missing or wrong
    Problem problem;                    // why it is not usable (code empty when it is)
    Contents contents;
    bool enabled{true};
    bool active{};                      // enabled and every check passed
    std::string id() const {return manifest?manifest->id:folder.filename().string();}
};

// What the player chose, kept in <mods>/mods.json: the order (bottom first) and the
// packages turned off. Packages not listed go on top in id order.
struct State {
    std::vector<std::string> order;
    std::set<std::string> disabled;
    static State parse(const json& data) {
        State s;
        if(!data.is_object() || data.value("schema","")!=state_schema)return s;
        for(const auto& id:data.value("order",json::array()))if(id.is_string())s.order.push_back(id.get<std::string>());
        for(const auto& id:data.value("disabled",json::array()))if(id.is_string())s.disabled.insert(id.get<std::string>());
        return s;
    }
    json to_json() const {return {{"schema",state_schema},{"order",order},{"disabled",disabled}};}
};

// Checks the enabled packages against each other and the game, and sorts them: a package
// comes after (above) everything it requires or optionally follows; otherwise the player's
// order holds. Returns them bottom first, every installed package listed once.
inline std::vector<Package> resolve(std::vector<Package> packages,const State& state,const Version& game) {
    for(auto& p:packages)p.enabled=!state.disabled.contains(p.id());
    // Two folders with one id: the first in folder order is used, the other is an error.
    std::map<std::string,size_t> by_id;
    for(size_t i=0;i<packages.size();++i) {
        auto& p=packages[i];
        if(!p.manifest)continue;
        if(!by_id.emplace(p.manifest->id,i).second){p.problem={"duplicate",p.manifest->id};p.manifest.reset();}
    }
    // The player's order first, then the rest by id.
    std::vector<size_t> base;
    std::set<size_t> placed;
    for(const auto& id:state.order)if(const auto f=by_id.find(id);f!=by_id.end() && placed.insert(f->second).second)base.push_back(f->second);
    std::vector<size_t> rest;
    for(size_t i=0;i<packages.size();++i)if(!placed.contains(i))rest.push_back(i);
    std::sort(rest.begin(),rest.end(),[&](size_t a,size_t b){return packages[a].id()<packages[b].id();});
    base.insert(base.end(),rest.begin(),rest.end());
    // Checks that need only the package and the game.
    // An unknown game version (0.0.0) checks nothing.
    for(auto& p:packages) {
        if(!p.manifest || !p.problem.code.empty() || game==Version{})continue;
        if(!satisfies(game,p.manifest->game))p.problem={"game",p.manifest->game};
    }
    // Requirements, repeated until nothing changes: a package whose requirement is not
    // usable is not usable either.
    const auto usable=[&](size_t i){return packages[i].manifest && packages[i].problem.code.empty() && packages[i].enabled;};
    for(bool changed=true;changed;) {
        changed=false;
        for(size_t i=0;i<packages.size();++i) {
            auto& p=packages[i];
            if(!usable(i))continue;
            for(const auto& [id,want]:p.manifest->needs) {
                const auto f=by_id.find(id);
                if(f==by_id.end()){p.problem={"missing",id+" "+want};changed=true;break;}
                const auto& d=packages[f->second];
                if(!satisfies(d.manifest->version,want)){p.problem={"dependency_version",id+" "+want};changed=true;break;}
                if(!usable(f->second)){p.problem={"dependency_off",id};changed=true;break;}
            }
        }
    }
    // Conflicts: of two enabled packages that conflict, the later in the base order is
    // turned away (either side may declare it).
    for(size_t a=0;a<base.size();++a)for(size_t b=a+1;b<base.size();++b) {
        auto& lower=packages[base[a]];auto& upper=packages[base[b]];
        if(!usable(base[a]) || !usable(base[b]))continue;
        const auto clash=[](const Package& x,const Package& y){
            const auto f=x.manifest->conflicts.find(y.manifest->id);
            return f!=x.manifest->conflicts.end() && satisfies(y.manifest->version,f->second);
        };
        if(clash(lower,upper) || clash(upper,lower))upper.problem={"conflict",lower.manifest->id};
    }
    // The load order: stable topological sort over the base order.
    std::vector<size_t> order;
    std::set<size_t> done,visiting;
    std::function<void(size_t)> visit=[&](size_t i) {
        if(done.contains(i))return;
        if(visiting.contains(i)){packages[i].problem={"cycle",packages[i].id()};return;}
        visiting.insert(i);
        if(packages[i].manifest) {
            std::vector<std::string> before;
            for(const auto& [id,w]:packages[i].manifest->needs)before.push_back(id);
            for(const auto& [id,w]:packages[i].manifest->follows)before.push_back(id);
            for(const auto& id:before)if(const auto f=by_id.find(id);f!=by_id.end())visit(f->second);
        }
        visiting.erase(i);
        if(done.insert(i).second)order.push_back(i);
    };
    for(const size_t i:base)visit(i);
    std::vector<Package> out;
    for(const size_t i:order) {
        packages[i].active=usable(i);
        out.push_back(std::move(packages[i]));
    }
    return out;
}

// What a package folder holds, by its known folders.
inline Contents summarize(const fs::path& folder) {
    Contents c;
    std::error_code error;
    const auto count=[&](const fs::path& dir,std::initializer_list<const char*> extensions) {
        unsigned n=0;
        if(!fs::is_directory(dir,error))return n;
        for(auto it=fs::recursive_directory_iterator(dir,error);it!=fs::recursive_directory_iterator();it.increment(error)) {
            if(error)break;
            if(!it->is_regular_file(error))continue;
            auto ext=it->path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch){return char(std::tolower(ch));});
            if(extensions.size()==0 || std::find(extensions.begin(),extensions.end(),ext)!=extensions.end())++n;
        }
        return n;
    };
    c.music=count(folder/"audio"/"music",{".ogg"});
    c.voices=count(folder/"audio"/"voice",{".ogg",".mp3",".flac",".wav"});
    c.art=count(folder/"art",{".png",".webp",".dds",".json"});
    c.dialogue=count(folder/"dialogue",{".txt"});
    c.data=count(folder/"data",{".json"});
    c.campaign=fs::is_regular_file(folder/"campaign"/"campaign.json",error);
    c.challenge=fs::is_directory(folder/"challenge",error);
    c.story=fs::is_directory(folder/"story",error);
    c.rules=fs::is_regular_file(folder/"rules.json",error);
    return c;
}

// A path inside a package archive, checked before anything is written: relative, no
// "..", no drive or root, '/' separated. Returns it without a leading "./".
inline std::optional<std::string> safe_entry(std::string name) {
    std::replace(name.begin(),name.end(),'\\','/');
    while(name.starts_with("./"))name.erase(0,2);
    if(name.empty() || name.front()=='/' || name.find(':')!=std::string::npos)return std::nullopt;
    for(size_t at=0;at<=name.size();) {
        const size_t end=name.find('/',at);
        const auto part=name.substr(at,end==std::string::npos?std::string::npos:end-at);
        if(part==".." || (part.empty() && end!=std::string::npos && end+1<name.size()))return std::nullopt;
        if(end==std::string::npos)break;
        at=end+1;
    }
    return name;
}

// mod_library.cpp: the player's packages on disk (SRW64_MODS_DIR, the user folder's
// mods/). Thread-safe; the window thread calls these.
struct InstallResult {
    bool ok{};
    std::string id;
    Version version;
    std::optional<Version> replaced;    // the version an update took the place of
    Problem problem;
};
fs::path directory();
// Every installed package, resolved against the game version (bottom first). Packages
// dropped into the folder as .srw64mod files are unpacked first.
std::vector<Package> installed(const Version& game);
// A .srw64mod (or .zip) file or a package folder, unpacked or copied into the folder and
// turned on. An installed package with the same id is replaced (the old one is kept in
// <mods>/.trash).
InstallResult install(const fs::path& source);
bool set_enabled(const std::string& id,bool on);
// Moves the package to <mods>/.trash.
bool remove(const std::string& id);
// Moves on every install, switch and removal, so readers of the packages know to look again.
unsigned revision();
// The game's version, for the packages' "game" requirement (the window thread sets it).
void set_game_version(const Version& v);
Version game_version();
}

