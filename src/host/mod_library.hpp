#pragma once
// MOD packages (docs/design/mod-packages.md §2–§3): the manifest, version requirements,
// dependencies and the load order. A package is a .srw64mod file, a zip of mod.json and the
// content folders, which an install keeps as <mods>/<id>.srw64mod and the game reads in
// place; or a folder <mods>/<id>/ of the same files, as an author works on it (and as the
// releases before 0.5.4 unpacked every package). Files reads either. This header reads
// nothing from disk but through Files, so the rules are tested without a game
// (tests/native_mod_library.cpp).
#include "campaign_library.hpp"
#include "json/json.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iterator>
#include <fstream>
#include <map>
#include <memory>
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
// Built-in requirements a package's "requires" may name besides packages: "srw64.hd", the HD
// images (mod-names-art-data.md §2.3.1). The host registers each once it knows (the renderer,
// as the image mode is set up): nullopt not usable, else its version (0.0.0 a pack without
// one, which satisfies any). A change moves revision().
inline constexpr const char* builtin_hd="srw64.hd";
inline bool is_builtin(const std::string& id){return id==builtin_hd;}
void set_builtin(const std::string& id,std::optional<Version> version);
std::optional<Version> builtin(const std::string& id);
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
    // At most three numbers: a release for one platform has a fourth ("0.5.3.1"), which
    // packages' requirements do not see.
    size_t end=0;
    for(int dots=0;end<text.size() && ((text[end]>='0' && text[end]<='9') || (text[end]=='.' && ++dots<3));)++end;
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
    bool campaign{},challenge{},story{},rules{},names{};
    std::vector<std::string> languages;   // dialogue/<locale>/ with language.json (mod_languages.hpp)
    bool empty() const {return !music && !voices && !art && !dialogue && !data && !campaign && !challenge && !story && !rules && !names && languages.empty();}
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
// A package's files wherever they are: its folder, or its .srw64mod read in place. Paths
// are relative to the package, '/' separated: "audio/music/12.ogg". Thread-safe.
struct Files {
    virtual ~Files()=default;
    // Every file the package holds, by its relative path, sorted.
    virtual const std::vector<std::string>& all() const=0;
    // One file's bytes; nullopt when it is not there or cannot be read.
    virtual std::optional<std::vector<unsigned char>> read(const std::string& path) const=0;
    // Where the package is (its folder or its file), for messages.
    virtual fs::path where() const=0;
    bool exists(const std::string& path) const {
        const auto& files=all();
        return std::binary_search(files.begin(),files.end(),path);
    }
    // The files directly in `folder` ("audio/music"), by name ("12.ogg"), sorted.
    std::vector<std::string> list(const std::string& folder) const {
        std::vector<std::string> out;
        const auto prefix=folder.empty()?std::string():folder+"/";
        const auto& files=all();
        for(auto it=std::lower_bound(files.begin(),files.end(),prefix);it!=files.end() && it->starts_with(prefix);++it)
            if(it->find('/',prefix.size())==std::string::npos)out.push_back(it->substr(prefix.size()));
        return out;
    }
    // The folders directly in `folder`, by name, sorted.
    std::vector<std::string> folders(const std::string& folder) const {
        std::set<std::string> out;
        const auto prefix=folder.empty()?std::string():folder+"/";
        const auto& files=all();
        for(auto it=std::lower_bound(files.begin(),files.end(),prefix);it!=files.end() && it->starts_with(prefix);++it)
            if(const auto slash=it->find('/',prefix.size());slash!=std::string::npos)out.insert(it->substr(prefix.size(),slash-prefix.size()));
        return {out.begin(),out.end()};
    }
    // Whether anything is under `folder`.
    bool has_folder(const std::string& folder) const {
        const auto prefix=folder+"/";
        const auto& files=all();
        const auto it=std::lower_bound(files.begin(),files.end(),prefix);
        return it!=files.end() && it->starts_with(prefix);
    }
    std::optional<std::string> read_text(const std::string& path) const {
        auto bytes=read(path);
        if(!bytes)return std::nullopt;
        return std::string(bytes->begin(),bytes->end());
    }
    // The JSON in a file; null when it is missing or not JSON.
    json read_json(const std::string& path) const {
        const auto text=read_text(path);
        if(!text)return json();
        auto data=json::parse(*text,nullptr,false);
        return data.is_discarded()?json():data;
    }
};
// A package folder's files, read from the disk as asked.
class FolderFiles:public Files {
public:
    explicit FolderFiles(fs::path folder):folder_(std::move(folder)) {
        std::error_code error;
        if(fs::is_directory(folder_,error))
            for(auto it=fs::recursive_directory_iterator(folder_,error);!error && it!=fs::recursive_directory_iterator();it.increment(error))
                if(it->is_regular_file(error)) {
                    auto name=fs::relative(it->path(),folder_,error).generic_string();
                    if(!error && !name.empty())files_.push_back(std::move(name));
                }
        std::sort(files_.begin(),files_.end());
    }
    const std::vector<std::string>& all() const override {return files_;}
    std::optional<std::vector<unsigned char>> read(const std::string& path) const override {
        if(path.empty() || path.find("..")!=std::string::npos)return std::nullopt;
        std::ifstream in(folder_/fs::path(path),std::ios::binary);
        if(!in)return std::nullopt;
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(in),{});
    }
    fs::path where() const override {return folder_;}
protected:
    fs::path folder_;
    std::vector<std::string> files_;
};

struct Package {
    fs::path folder;                    // the package's folder or its .srw64mod file
    std::shared_ptr<const Files> files; // set by installed(); files_of() for a Package made by hand
    std::optional<Manifest> manifest;   // empty when mod.json is missing or wrong
    Problem problem;                    // why it is not usable (code empty when it is)
    Contents contents;
    bool enabled{true};
    bool active{};                      // enabled and every check passed
    std::string id() const {return manifest?manifest->id:(folder.extension()==package_extension?folder.stem():folder.filename()).string();}
    bool archived() const {return folder.extension()==package_extension;}
};
// A package's files: the ones the listing found, or its folder's for a Package made by hand
// (tests, and the packages a campaign folder holds).
inline std::shared_ptr<const Files> files_of(const Package& p) {
    return p.files?p.files:std::make_shared<FolderFiles>(p.folder);
}

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
                if(is_builtin(id)) {
                    const auto v=builtin(id);
                    if(!v){p.problem={"needs_hd",id};changed=true;break;}
                    if(*v!=Version{} && !satisfies(*v,want)){p.problem={"dependency_version",id+" "+want};changed=true;break;}
                    continue;
                }
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

// A package's files on the disk: a .srw64mod read in place (mod_library.cpp), or a folder.
// A folder the releases before 0.5.4 unpacked may hold its voices in voice.srw64mod
// (voice_archive): they show as its audio/voice/ files.
inline constexpr const char* voice_archive="voice.srw64mod";
std::shared_ptr<const Files> open_files(const fs::path& where);
// A path the code hands to something that opens files itself (a font face): a path into a
// package file comes out of it once, into <mods>/.cache/<package>-<size>-<time>/ (the folder of
// an older copy of that package goes then); any other path is itself. Empty when it cannot.
fs::path local_file(const fs::path& path);
// When a file was written: for a path into a package file, the package file's time (a picture
// loaded once is used again until its package changes).
fs::file_time_type file_time(const fs::path& path);

// What a package holds, by its known folders.
inline Contents summarize(const Files& files) {
    Contents c;
    const auto known=[](const std::string& file,std::initializer_list<const char*> extensions) {
        auto ext=fs::path(file).extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch){return char(std::tolower(ch));});
        return extensions.size()==0 || std::find(extensions.begin(),extensions.end(),ext)!=extensions.end();
    };
    const auto count=[&](const std::string& folder,std::initializer_list<const char*> extensions) {
        unsigned n=0;
        const auto prefix=folder+"/";
        for(const auto& file:files.all())if(file.starts_with(prefix) && known(file,extensions))++n;
        return n;
    };
    c.music=count("audio/music",{".ogg"});
    c.voices=count("audio/voice",{".ogg",".mp3",".flac",".wav"});
    c.art=count("art",{".png",".webp",".dds",".json"});
    c.dialogue=count("dialogue",{".txt"});
    c.names=files.exists("dialogue/names.json");
    for(const auto& locale:files.folders("dialogue"))
        if(files.exists("dialogue/"+locale+"/language.json"))c.languages.push_back(locale);
    c.data=count("data",{".json"});
    c.campaign=files.exists("campaign/campaign.json");
    c.challenge=files.has_folder("challenge");
    c.story=files.has_folder("story");
    c.rules=files.exists("rules.json");
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
// Every installed package, resolved against the game version (bottom first): each
// <id>.srw64mod and <id>/ in the folder, the folder taking the place of a file of the same id
// (an author's working copy). Package files placed in the folder under another name, or as a
// .zip, are installed first (renamed into place), and a package folder put there under another
// name (a package extracted by hand: "srw64.space-a-1.0.2/") takes its id's name, replacing an
// installed one of that id as an install does.
std::vector<Package> installed(const Version& game);
// Told 0 to 100 as the files placed in the folder install, by their bytes (the loading screen
// shows it: a Thunderstore zip's package is copied out of it).
void set_unpack_progress(void (*report)(unsigned percent));
// What the last listing found placed in the folder as files: the ones installed and the ones
// that did not (still there, tried again next time).
struct LooseResult {
    fs::path file;
    InstallResult result;
};
std::vector<LooseResult> loose_results();
// A .srw64mod (or .zip) file, checked and kept as <mods>/<id>.srw64mod, or a package folder,
// copied in as <mods>/<id>/; turned on. An installed package with the same id is replaced (the
// old one is kept in <mods>/.trash).
InstallResult install(const fs::path& source);
bool set_enabled(const std::string& id,bool on);
// Moves the package (its folder and its file) to <mods>/.trash.
bool remove(const std::string& id);
// Moves on every install, switch and removal, so readers of the packages know to look again.
unsigned revision();
// The game's version, for the packages' "game" requirement (the window thread sets it).
void set_game_version(const Version& v);
Version game_version();
}

