// MOD packages (src/host/mod_library.hpp, docs/design/mod-packages.md §2–§3): the
// manifest rules, version requirements, dependencies and load order, archive paths, and
// installing from a .srw64mod file and a folder into a scratch mods folder.
#include "mod_library.hpp"
#include "miniz.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace srw64;
using json=nlohmann::json;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}

static json manifest(const std::string& id,const std::string& version="1.0.0",json extra=json::object()) {
    json m={{"schema","srw64.mod.v1"},{"id",id},{"version",version},{"name",{{"en",id}}}};
    m.update(extra);
    return m;
}
static std::string problem_of(const json& m) {
    const auto parsed=mods::parse_manifest(m);
    const auto* p=std::get_if<mods::Problem>(&parsed);
    return p?p->code:"";
}
static mods::Package package(const json& m) {
    mods::Package p;
    p.folder=std::get<mods::Manifest>(mods::parse_manifest(m)).id;
    p.manifest=std::get<mods::Manifest>(mods::parse_manifest(m));
    return p;
}
static std::vector<std::string> ids(const std::vector<mods::Package>& list) {
    std::vector<std::string> out;
    for(const auto& p:list)out.push_back(p.id());
    return out;
}
static const mods::Package& find(const std::vector<mods::Package>& list,const std::string& id) {
    for(const auto& p:list)if(p.id()==id)return p;
    std::fprintf(stderr,"no package %s\n",id.c_str());std::abort();
}
static void write(const std::filesystem::path& path,const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
static void zip_file(const std::filesystem::path& path,const std::vector<std::pair<std::string,std::string>>& files) {
    mz_zip_archive zip{};
    mz_zip_writer_init_file(&zip,path.string().c_str(),0);
    for(const auto& [name,text]:files)mz_zip_writer_add_mem(&zip,name.c_str(),text.data(),text.size(),MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
}

int main(int argc,char** argv) {
    const mods::Version game{0,4,9};
    // Versions and requirements.
    check(mods::parse_version("1.2.3")==mods::Version{1,2,3} && mods::parse_version("2")==mods::Version{2,0,0},"versions parse");
    check(!mods::parse_version("1.x") && !mods::parse_version("") && !mods::parse_version("1.2.3.4"),"bad versions refused");
    check(mods::leading_version("0.4.9-dirty")==mods::Version{0,4,9},"the game's release name");
    check(mods::leading_version("0.5.3.1")==mods::Version{0,5,3},"a one-platform release's fourth number left out");
    check(mods::satisfies({1,2,0},">=1.0 <2") && !mods::satisfies({2,0,0},">=1.0 <2") && mods::satisfies({0,1,0},"*"),"requirements");
    check(!mods::valid_requirement("~1.0") && !mods::valid_requirement("") && mods::valid_requirement("=1.2.3"),"requirement syntax");
    // The manifest, strictly.
    check(problem_of(manifest("a.b")).empty(),"a minimal manifest");
    check(problem_of(manifest("a.b","1.0",{{"colour","red"}}))=="field","unknown fields refused");
    check(problem_of(manifest("a/b"))=="id","an id is a folder name");
    check(problem_of(manifest("a.b","one"))=="version","a version is numbers");
    check(problem_of(manifest("a.b","1.0",{{"name",""}}))=="name","a name is needed");
    check(problem_of(manifest("a.b","1.0",{{"requires",{{"a.b","*"}}}}))=="requires","a package does not require itself");
    check(problem_of(manifest("a.b","1.0",{{"game","newer"}}))=="game","a game requirement is a requirement");
    check(problem_of(json{{"schema","srw64.mod.v2"}})=="schema","the schema is v1");
    check(problem_of(manifest("a.b","1.0",{{"license","CC-BY-NC-4.0"}})).empty() && problem_of(manifest("a.b","1.0",{{"license","All rights reserved"}}))=="license","a licence is one of the list");
    // The manifest cases shared with tools/mods/validate.py (tests/mods/manifests.json).
    if(argc>2) {
        json cases;std::ifstream(argv[2])>>cases;
        unsigned n=0;
        for(const auto& c:cases["cases"]){check(problem_of(c["manifest"])==c["problem"].get<std::string>(),"fixture "+c["case"].get<std::string>()+": got \""+problem_of(c["manifest"])+"\"");++n;}
        check(n>=10,"the shared manifest cases were read");
    } else std::fprintf(stderr,"note: no manifest fixture given\n");

    // Resolving: dependencies below their dependents whatever the player's order.
    {
        std::vector<mods::Package> list{
            package(manifest("x.top","1.0",{{"requires",{{"x.base",">=1.0"}}}})),
            package(manifest("x.base","1.2")),
            package(manifest("x.loose")),
        };
        mods::State state;state.order={"x.top","x.loose","x.base"};
        const auto out=mods::resolve(list,state,game);
        check(ids(out)==std::vector<std::string>{"x.base","x.top","x.loose"},"a dependency goes below its dependent");
        check(find(out,"x.top").active && find(out,"x.base").active,"both active");
    }
    // A missing, too old or turned-off requirement turns the dependent away.
    {
        std::vector<mods::Package> list{package(manifest("y.top","1.0",{{"requires",{{"y.base",">=2"}}}})),package(manifest("y.base","1.0"))};
        const auto out=mods::resolve(list,{},game);
        check(!find(out,"y.top").active && find(out,"y.top").problem.code=="dependency_version","too old a requirement");
        std::vector<mods::Package> list2{package(manifest("y.top","1.0",{{"requires",{{"y.base","*"}}}})),package(manifest("y.base","1.0"))};
        mods::State off;off.disabled={"y.base"};
        const auto out2=mods::resolve(list2,off,game);
        check(find(out2,"y.top").problem.code=="dependency_off" && !find(out2,"y.base").active,"a turned-off requirement");
        std::vector<mods::Package> list3{package(manifest("y.top","1.0",{{"requires",{{"y.gone","*"}}}}))};
        check(mods::resolve(list3,{},game)[0].problem.code=="missing","a missing requirement");
    }
    // The built-in HD requirement: off until the host registers it, then by its version.
    {
        std::vector<mods::Package> list{package(manifest("h.art","1.0",{{"requires",{{"srw64.hd",">=1.3"}}}}))};
        mods::set_builtin(mods::builtin_hd,std::nullopt);
        check(mods::resolve(list,{},game)[0].problem.code=="needs_hd","no HD, no package");
        mods::set_builtin(mods::builtin_hd,mods::Version{1,2,0});
        check(mods::resolve(list,{},game)[0].problem.code=="dependency_version","too old an HD pack");
        mods::set_builtin(mods::builtin_hd,mods::Version{});
        check(mods::resolve(list,{},game)[0].active,"an HD pack without a version satisfies");
        const unsigned before=mods::revision();
        mods::set_builtin(mods::builtin_hd,mods::Version{1,3,0});
        check(mods::resolve(list,{},game)[0].active && mods::revision()!=before,"registering moves the revision");
        mods::set_builtin(mods::builtin_hd,std::nullopt);
    }
    // Conflicts, the game version, and an optional package only ordering.
    {
        std::vector<mods::Package> list{package(manifest("z.one","1.0",{{"conflicts",{{"z.two","*"}}}})),package(manifest("z.two")),
                                        package(manifest("z.new","1.0",{{"game",">=0.5"}})),
                                        package(manifest("z.after","1.0",{{"optional",{{"z.two","*"},{"z.absent","*"}}}}))};
        mods::State state;state.order={"z.after","z.one","z.two"};
        const auto out=mods::resolve(list,state,game);
        check(find(out,"z.one").active && find(out,"z.two").problem.code=="conflict","the later of two conflicting packages is turned away");
        check(find(out,"z.new").problem.code=="game","a package for a newer game");
        const auto order=ids(out);
        check(std::find(order.begin(),order.end(),"z.two")<std::find(order.begin(),order.end(),"z.after") && find(out,"z.after").active,
              "an optional package orders without being required");
    }
    // Archive paths.
    check(mods::safe_entry("audio/music/7.ogg")==std::string("audio/music/7.ogg") && mods::safe_entry("./mod.json")==std::string("mod.json"),"plain paths");
    check(!mods::safe_entry("../evil") && !mods::safe_entry("/etc/x") && !mods::safe_entry("a/../../b") && !mods::safe_entry("C:/x") && !mods::safe_entry("a\\..\\b"),
          "paths leaving the folder refused");

    // On disk: install a .srw64mod (inside one top folder) and a folder, update, turn off, remove.
    {
        namespace fs=std::filesystem;
        const fs::path root=argc>1?fs::path(argv[1]):fs::temp_directory_path()/"srw64-mod-test";
        fs::remove_all(root);
        fs::create_directories(root);
        const auto library=root/"mods";
        setenv("SRW64_MODS_DIR",library.string().c_str(),1);
        zip_file(root/"music.srw64mod",{{"pack/mod.json",manifest("test.music","1.0.0").dump()},{"pack/audio/music/7.ogg","ogg"},{"pack/audio/music/12.mp3","mp3"},
                                       {"__MACOSX/pack/._7.ogg","junk"}});
        auto r=mods::install(root/"music.srw64mod");
        check(r.ok && r.id=="test.music" && !r.replaced,"a package file installs: "+r.problem.code+" "+r.problem.detail);
        check(fs::is_regular_file(library/"test.music"/"audio"/"music"/"7.ogg") && !fs::exists(library/"test.music"/"__MACOSX"),"unpacked under its id, junk left out");
        auto list=mods::installed(game);
        check(list.size()==1 && list[0].active && list[0].contents.music==1,"listed with its music (only Ogg counts)");
        std::filesystem::create_directories(library/"test.music"/"campaign");
        check(!mods::summarize(library/"test.music").campaign,"an empty campaign folder is not a campaign");
        write(library/"test.music"/"campaign"/"campaign.json","{}");
        check(mods::summarize(library/"test.music").campaign,"a campaign is its image");
        // A newer version replaces it; the old one is kept in .trash.
        zip_file(root/"music2.zip",{{"mod.json",manifest("test.music","1.1.0").dump()},{"audio/music/7.ogg","ogg2"}});
        r=mods::install(root/"music2.zip");
        check(r.ok && r.replaced==mods::Version{1,0,0},"an update replaces the old version");
        check(mods::installed(game)[0].manifest->version==mods::Version{1,1,0} && fs::is_directory(library/".trash"),"the old one is in .trash");
        // A folder.
        write(root/"src"/"voice"/"mod.json",manifest("test.voice","1.0",{{"requires",{{"test.music",">=1.1"}}}}).dump());
        write(root/"src"/"voice"/"audio"/"voice"/"battle"/"14227.ogg","v");
        r=mods::install(root/"src"/"voice");
        list=mods::installed(game);
        check(r.ok && ids(list)==std::vector<std::string>{"test.music","test.voice"} && find(list,"test.voice").active,"a folder installs above what it requires");
        // A bad archive changes nothing.
        zip_file(root/"bad.srw64mod",{{"../mod.json",manifest("test.bad").dump()}});
        r=mods::install(root/"bad.srw64mod");
        check(!r.ok && r.problem.code=="archive_path" && !fs::exists(library/"test.bad"),"a path leaving the folder refused");
        zip_file(root/"nomanifest.srw64mod",{{"audio/music/1.ogg","x"}});
        check(mods::install(root/"nomanifest.srw64mod").problem.code=="no_manifest","no mod.json refused");
        // Off and on; removing.
        mods::set_enabled("test.music",false);
        list=mods::installed(game);
        check(!find(list,"test.music").active && find(list,"test.voice").problem.code=="dependency_off","turning off a requirement");
        mods::set_enabled("test.music",true);
        check(mods::remove("test.voice") && mods::installed(game).size()==1,"removed to .trash");
        // Package files dropped into the folder are unpacked on the next look, the progress
        // told from 0 to 100 by their bytes.
        zip_file(library/"loose.srw64mod",{{"mod.json",manifest("test.loose").dump()}});
        zip_file(library/"loose2.srw64mod",{{"mod.json",manifest("test.loose2").dump()},{"audio/voice/battle/1.ogg",std::string(4096,'v')}});
        static std::vector<unsigned> told;
        mods::set_unpack_progress([](unsigned percent){told.push_back(percent);});
        list=mods::installed(game);
        mods::set_unpack_progress(nullptr);
        check(list.size()==3 && !fs::exists(library/"loose.srw64mod") && !fs::exists(library/"loose2.srw64mod"),"loose package files are unpacked");
        check(!told.empty() && told.front()==0 && told.back()==100 && std::is_sorted(told.begin(),told.end()),"the unpacking told from 0 to 100");
        told.clear();
        mods::installed(game);
        check(told.empty(),"nothing told with nothing to unpack");
        // A package file with voices stays whole inside the package and they are read from
        // it; everything else unpacks, and the file goes when the package goes.
        {
            const auto voice_file=library/"test.loose2"/mods::voice_archive;
            check(fs::is_regular_file(voice_file) && !fs::exists(library/"test.loose2"/"audio"/"voice"),"a placed package file moves in with its voices");
            check(find(list,"test.loose2").contents.voices==1,"its lines are counted");
            check(mods::archive_voices(voice_file)==std::vector<std::string>{"audio/voice/battle/1.ogg"},"listed from the file");
            const auto line=mods::read_archive(voice_file,"audio/voice/battle/1.ogg");
            check(line && *line==std::vector<unsigned char>(4096,'v'),"one line read out of it");
            check(!mods::read_archive(voice_file,"audio/voice/battle/2.ogg"),"a line it does not have");
            check(fs::is_regular_file(library/"test.loose"/"mod.json") && !fs::exists(library/"test.loose"/mods::voice_archive),"a package without voices keeps no file");
            // From elsewhere, in one top folder: copied, the player's file left alone. The
            // rest unpacks, voice.json beside the folder among it.
            zip_file(root/"voices.srw64mod",{{"pack/mod.json",manifest("test.packed","1.0.0").dump()},{"pack/audio/voice.json","{}"},{"pack/audio/music/7.ogg","ogg"},
                                             {"pack/audio/voice/battle/5799.ogg","first"},{"pack/audio/voice/battle/5800.OGG","second"},{"__MACOSX/pack/audio/voice/battle/._5799.ogg","junk"}});
            auto r=mods::install(root/"voices.srw64mod");
            const auto packed=library/"test.packed"/mods::voice_archive;
            check(r.ok && r.voices && fs::exists(root/"voices.srw64mod") && fs::is_regular_file(packed),"a file from elsewhere is copied");
            check(fs::is_regular_file(library/"test.packed"/"audio"/"voice.json") && fs::is_regular_file(library/"test.packed"/"audio"/"music"/"7.ogg") && !fs::exists(library/"test.packed"/"audio"/"voice"),"only audio/voice stays packed");
            check(mods::archive_voices(packed)==std::vector<std::string>{"pack/audio/voice/battle/5799.ogg","pack/audio/voice/battle/5800.OGG"},"named as the file names them, no junk");
            check(find(mods::installed(game),"test.packed").contents.voices==2 && find(mods::installed(game),"test.packed").contents.music==1,"the count sees both");
            // An update under the same name: the open file is let go, the new one read.
            zip_file(root/"voices2.srw64mod",{{"mod.json",manifest("test.packed","1.1.0").dump()},{"audio/voice/battle/5799.ogg","updated"}});
            const auto before=mods::revision();
            r=mods::install(root/"voices2.srw64mod");
            const auto again=mods::read_archive(packed,"audio/voice/battle/5799.ogg");
            check(r.ok && r.replaced && mods::revision()!=before && again && std::string(again->begin(),again->end())=="updated","an update reads the new file");
            check(!mods::read_archive(packed,"pack/audio/voice/battle/5799.ogg"),"the old file is gone from the table");
            // An update without voices leaves none behind.
            zip_file(root/"voices3.srw64mod",{{"mod.json",manifest("test.packed","1.2.0").dump()}});
            check(mods::install(root/"voices3.srw64mod").ok && !fs::exists(packed) && mods::archive_voices(packed).empty(),"an update without voices has no file");
            r=mods::install(root/"voices2.srw64mod");
            check(r.ok && mods::read_archive(packed,"audio/voice/battle/5799.ogg") && mods::remove("test.packed") && !fs::exists(library/"test.packed") && !mods::read_archive(packed,"audio/voice/battle/5799.ogg"),"removed, it reads nothing");
            check(!mods::remove("test.loose2") || !fs::exists(voice_file),"the file goes to .trash with its package");
            list=mods::installed(game);
        }
        // The listing is kept until the folder changes.
        {
            write(library/"test.kept"/"mod.json",manifest("test.kept").dump());
            check(find(mods::installed(game),"test.kept").contents.empty(),"a new folder is seen");
            write(library/"test.kept"/"audio"/"music"/"7.ogg","ogg");
            check(find(mods::installed(game),"test.kept").contents.empty(),"a file added by hand waits for a new look");
            mods::set_enabled("test.kept",false);
            check(!find(mods::installed(game),"test.kept").active,"the player's choices are read each time");
            mods::set_enabled("test.kept",true);
            const auto when=fs::last_write_time(library/"test.kept"/"mod.json");
            fs::last_write_time(library/"test.kept"/"mod.json",when+std::chrono::seconds(5));
            check(find(mods::installed(game),"test.kept").contents.music==1,"a rewritten manifest is read again");
            check(mods::remove("test.kept"),"removed");
            for(const auto& p:mods::installed(game))check(p.id()!="test.kept","gone from the listing");
            list=mods::installed(game);
        }
        // A package extracted by hand takes its id's name: as the extractor names the folder,
        // or one level deeper (with the macOS junk beside it).
        write(library/"test.right-1.0.0"/"mod.json",manifest("test.right").dump());
        write(library/"test.deep-1.0.0"/"test.deep-1.0.0"/"mod.json",manifest("test.deep").dump());
        write(library/"test.deep-1.0.0"/"__MACOSX"/"._mod.json","junk");
        list=mods::installed(game);
        check(find(list,"test.right").active && fs::is_regular_file(library/"test.right"/"mod.json") && !fs::exists(library/"test.right-1.0.0"),
              "an extracted folder is renamed to its id");
        check(find(list,"test.deep").active && fs::is_regular_file(library/"test.deep"/"mod.json") && !fs::exists(library/"test.deep-1.0.0"),
              "a folder inside the extracted one is taken out, its shell kept in .trash");
        // An extracted newer version replaces the installed one, which goes to .trash.
        write(library/"test.right-1.1.0"/"mod.json",manifest("test.right","1.1.0").dump());
        list=mods::installed(game);
        check(find(list,"test.right").active && find(list,"test.right").manifest->version==mods::Version{1,1,0} && !fs::exists(library/"test.right-1.1.0"),
              "an extracted folder replaces the installed version");
        fs::remove_all(root);
    }
    if(failures)std::fprintf(stderr,"%d failed\n",failures);
    else std::puts("mod library: ok");
    return failures?1:0;
}
