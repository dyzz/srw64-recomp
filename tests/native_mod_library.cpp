// MOD packages (src/host/mod_library.hpp, docs/design/mod-packages.md §2–§3): the
// manifest rules, version requirements, dependencies and load order, archive paths, and
// installing from a .srw64mod file and a folder into a scratch mods folder.
#include "../src/native/presentation/file_source.hpp"
#include <chrono>
#include <thread>
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
    check(!mods::parse_version("1.x") && !mods::parse_version("") && !mods::parse_version("1.2.3.4.5"),"bad versions refused");
    check(mods::parse_version("0.5.4.1")==mods::Version{0,5,4,1} && mods::version_text(mods::Version{0,5,4,1})=="0.5.4.1" &&
          mods::version_text(mods::Version{1,2,0})=="1.2.0","a fix release's fourth number");
    check(mods::leading_version("0.4.9-dirty")==mods::Version{0,4,9},"the game's release name");
    check(mods::leading_version("0.5.4.1")==mods::Version{0,5,4,1} && mods::leading_version("0.5.4.1-dirty")==mods::Version{0,5,4,1},
          "a fix release's fourth number counts");
    check(mods::satisfies(mods::Version{0,5,4,1},">=0.5.4.1") && !mods::satisfies(mods::Version{0,5,4},">=0.5.4.1") &&
          mods::satisfies(mods::Version{0,5,4,1},">=0.5.4") && !mods::satisfies(mods::Version{0,5,4,1},">=0.5.5"),"a fix release against requirements");
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

    // On disk: a package file is kept as <id>.srw64mod and read in place; a folder is copied in.
    {
        namespace fs=std::filesystem;
        const fs::path root=argc>1?fs::path(argv[1]):fs::temp_directory_path()/"srw64-mod-test";
        fs::remove_all(root);
        fs::create_directories(root);
        const auto library=root/"mods";
        setenv("SRW64_MODS_DIR",library.string().c_str(),1);
        const auto text=[](const std::optional<std::vector<unsigned char>>& bytes){return bytes?std::string(bytes->begin(),bytes->end()):std::string("<none>");};
        zip_file(root/"music.srw64mod",{{"pack/mod.json",manifest("test.music","1.0.0").dump()},{"pack/audio/music/7.ogg","ogg"},{"pack/audio/music/12.mp3","mp3"},
                                       {"__MACOSX/pack/._7.ogg","junk"}});
        auto r=mods::install(root/"music.srw64mod");
        check(r.ok && r.id=="test.music" && !r.replaced,"a package file installs: "+r.problem.code+" "+r.problem.detail);
        const auto music=library/"test.music.srw64mod";
        check(fs::is_regular_file(music) && !fs::exists(library/"test.music") && fs::exists(root/"music.srw64mod"),"kept whole under its id, the player's file left alone");
        auto list=mods::installed(game);
        check(list.size()==1 && list[0].active && list[0].archived() && list[0].contents.music==1,"listed with its music (only Ogg counts)");
        const auto files=mods::files_of(list[0]);
        check(files->all()==std::vector<std::string>{"audio/music/12.mp3","audio/music/7.ogg","mod.json"},"its files, out of the top folder, no junk");
        check(text(files->read("audio/music/7.ogg"))=="ogg" && files->list("audio/music").size()==2 && files->folders("audio")==std::vector<std::string>{"music"},"read in place");
        // A path into the file reads as a folder of it, for code that names files by path.
        check(text(srw64::presentation::read_file(music/"audio"/"music"/"7.ogg"))=="ogg","a path into the package file");
        check(srw64::presentation::file_exists(music/"mod.json") && !srw64::presentation::file_exists(music/"nothing.ogg"),"which files are there");
        check(srw64::presentation::list_files(music/"audio")==std::vector<fs::path>{music/"audio/music/12.mp3",music/"audio/music/7.ogg"},"a folder of it listed");
        // A newer version replaces it; the old one is kept in .trash.
        zip_file(root/"music2.zip",{{"mod.json",manifest("test.music","1.1.0").dump()},{"audio/music/7.ogg","ogg2"},{"fonts/a.ttf","font1"}});
        r=mods::install(root/"music2.zip");
        check(r.ok && r.replaced==mods::Version{1,0,0},"an update replaces the old version");
        list=mods::installed(game);
        check(list[0].manifest->version==mods::Version{1,1,0} && fs::is_directory(library/".trash") && !fs::exists(library/"test.music"),"the old one is in .trash");
        check(text(files->read("audio/music/7.ogg"))=="<none>","the old file's reader reads nothing once it moved");
        check(text(mods::files_of(list[0])->read("audio/music/7.ogg"))=="ogg2" && text(srw64::presentation::read_file(music/"audio/music/7.ogg"))=="ogg2","the new one is read");
        // A file the code opens itself (a font) comes out once, into the cache.
        {
            const auto font=mods::local_file(music/"fonts"/"a.ttf");
            std::ifstream in(font,std::ios::binary);
            check(font.parent_path().parent_path().parent_path()==library/".cache" && std::string(std::istreambuf_iterator<char>(in),{})=="font1","a font out of the package");
            check(mods::local_file(music/"fonts"/"a.ttf")==font && mods::local_file(root/"music2.zip")==root/"music2.zip","once; any other path is itself");
            zip_file(root/"music3.srw64mod",{{"mod.json",manifest("test.music","1.2.0").dump()},{"audio/music/7.ogg","ogg3"},{"fonts/a.ttf","font2!"}});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            check(mods::install(root/"music3.srw64mod").ok,"another update");
            const auto again=mods::local_file(music/"fonts"/"a.ttf");
            std::ifstream in2(again,std::ios::binary);
            check(again!=font && !fs::exists(font) && std::string(std::istreambuf_iterator<char>(in2),{})=="font2!","an update's font, the old copy gone");
        }
        // A folder.
        write(root/"src"/"voice"/"mod.json",manifest("test.voice","1.0",{{"requires",{{"test.music",">=1.1"}}}}).dump());
        write(root/"src"/"voice"/"audio"/"voice"/"battle"/"14227.ogg","v");
        r=mods::install(root/"src"/"voice");
        list=mods::installed(game);
        check(r.ok && ids(list)==std::vector<std::string>{"test.music","test.voice"} && find(list,"test.voice").active && !find(list,"test.voice").archived(),
              "a folder installs above what it requires");
        check(text(mods::files_of(find(list,"test.voice"))->read("audio/voice/battle/14227.ogg"))=="v","a folder's files through the same reader");
        std::filesystem::create_directories(library/"test.voice"/"campaign");
        check(!mods::summarize(mods::FolderFiles(library/"test.voice")).campaign,"an empty campaign folder is not a campaign");
        write(library/"test.voice"/"campaign"/"campaign.json","{}");
        check(mods::summarize(mods::FolderFiles(library/"test.voice")).campaign,"a campaign is its image");
        // A bad archive changes nothing.
        zip_file(root/"bad.srw64mod",{{"../mod.json",manifest("test.bad").dump()}});
        r=mods::install(root/"bad.srw64mod");
        check(!r.ok && r.problem.code=="archive_path" && !fs::exists(library/"test.bad.srw64mod"),"a path leaving the folder refused");
        zip_file(root/"nomanifest.srw64mod",{{"audio/music/1.ogg","x"}});
        check(mods::install(root/"nomanifest.srw64mod").problem.code=="no_manifest","no mod.json refused");
        // A Thunderstore package: its own manifest.json, icon and README beside the .srw64mod.
        {
            const auto bytes=[](const fs::path& p){std::ifstream in(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
            zip_file(root/"inner-a.srw64mod",{{"mod.json",manifest("test.ts-a","1.0.0").dump()},{"audio/music/3.ogg","a"}});
            zip_file(root/"inner-b.srw64mod",{{"b/mod.json",manifest("test.ts-b","2.0.0").dump()},{"b/audio/voice/battle/1.ogg","v"}});
            const std::string thunderstore_manifest=R"({"name":"Ts_A","version_number":"1.0.0","website_url":"","description":"x","dependencies":[]})";
            zip_file(root/"Team-Ts_A-1.0.0.zip",{{"manifest.json",thunderstore_manifest},{"icon.png","png"},{"README.md","# A"},{"test.ts-a-1.0.0.srw64mod",bytes(root/"inner-a.srw64mod")}});
            r=mods::install(root/"Team-Ts_A-1.0.0.zip");
            check(r.ok && r.id=="test.ts-a" && r.version==mods::Version{1,0,0},"a Thunderstore package installs the package inside: "+r.problem.code+" "+r.problem.detail);
            check(bytes(library/"test.ts-a.srw64mod")==bytes(root/"inner-a.srw64mod"),"the package file as it was made, not the wrapper");
            // Two in one zip, in a folder.
            zip_file(root/"two.zip",{{"README.md","both"},{"pkgs/a.srw64mod",bytes(root/"inner-a.srw64mod")},{"pkgs/b.SRW64MOD",bytes(root/"inner-b.srw64mod")}});
            r=mods::install(root/"two.zip");
            check(r.ok && fs::is_regular_file(library/"test.ts-a.srw64mod") && fs::is_regular_file(library/"test.ts-b.srw64mod"),"every package in a zip installs");
            check(find(mods::installed(game),"test.ts-b").contents.voices==1,"a package's voices counted from its file");
            // A bad one inside: refused, and no staging left behind.
            zip_file(root/"bad-inner.zip",{{"manifest.json",thunderstore_manifest},{"x.srw64mod","not a zip"}});
            r=mods::install(root/"bad-inner.zip");
            check(!r.ok && r.problem.code=="archive","a broken package inside is refused: "+r.problem.code);
            bool staging=false;
            for(const auto& item:fs::directory_iterator(library))staging|=item.path().filename().string().starts_with(".installing-");
            check(!staging,"nothing left being installed");
            // Placed in the folder, the zip goes to .trash once its package is in.
            mods::remove("test.ts-a");
            fs::copy_file(root/"Team-Ts_A-1.0.0.zip",library/"Team-Ts_A-1.0.0.zip");
            const auto placed=mods::installed(game);
            check(!fs::exists(library/"Team-Ts_A-1.0.0.zip") && find(placed,"test.ts-a").active,"a placed Thunderstore package installs");
            mods::remove("test.ts-a");mods::remove("test.ts-b");
        }
        // Off and on; removing.
        mods::set_enabled("test.music",false);
        list=mods::installed(game);
        check(!find(list,"test.music").active && find(list,"test.voice").problem.code=="dependency_off","turning off a requirement");
        mods::set_enabled("test.music",true);
        check(mods::remove("test.voice") && mods::installed(game).size()==1,"removed to .trash");
        // Package files dropped into the folder under other names are renamed into place on the
        // next look, the progress told from 0 to 100 by their bytes; one named for its id stays.
        zip_file(library/"loose-1.0.srw64mod",{{"mod.json",manifest("test.loose").dump()}});
        zip_file(library/"loose2.zip",{{"mod.json",manifest("test.loose2").dump()},{"audio/voice/battle/1.ogg",std::string(4096,'v')}});
        zip_file(library/"test.other.srw64mod",{{"mod.json",manifest("test.named").dump()}});
        zip_file(library/"test.placed.srw64mod",{{"mod.json",manifest("test.placed").dump()}});
        static std::vector<unsigned> told;
        mods::set_unpack_progress([](unsigned percent){told.push_back(percent);});
        list=mods::installed(game);
        mods::set_unpack_progress(nullptr);
        check(ids(list).size()==5 && fs::is_regular_file(library/"test.loose.srw64mod") && fs::is_regular_file(library/"test.loose2.srw64mod") &&
              !fs::exists(library/"loose-1.0.srw64mod") && !fs::exists(library/"loose2.zip"),"loose package files take their ids' names");
        check(fs::is_regular_file(library/"test.named.srw64mod") && !fs::exists(library/"test.other.srw64mod"),"a file named for another id too");
        check(find(list,"test.placed").active,"one already named for its id is installed as it is");
        check(!told.empty() && told.front()==0 && told.back()==100 && std::is_sorted(told.begin(),told.end()),"the placing told from 0 to 100");
        told.clear();
        mods::installed(game);
        check(told.empty(),"nothing told with nothing to place");
        check(find(list,"test.loose2").contents.voices==1 && text(mods::files_of(find(list,"test.loose2"))->read("audio/voice/battle/1.ogg"))==std::string(4096,'v'),
              "its voices read from the package file");
        // Something else given the extension (the Android picker names a ROM rom.z64.srw64mod)
        // stays and is reported, once: not listed as a package as well.
        {
            write(library/"rom.z64.srw64mod",std::string(4096,'r'));
            const auto before=ids(list).size();
            list=mods::installed(game);
            const auto results=mods::loose_results();
            check(ids(list).size()==before && fs::exists(library/"rom.z64.srw64mod") &&
                  std::count_if(results.begin(),results.end(),[](const auto& r){return !r.result.ok && r.file.filename()=="rom.z64.srw64mod";})==1,
                  "a file that does not install is reported, not listed");
            list=mods::installed(game);
            check(ids(list).size()==before,"nor on the next look");
            // The page's removal: only a reported failure, by its name, into .trash.
            check(!mods::remove_failed("test.loose2.srw64mod") && !mods::remove_failed("../rom.z64.srw64mod"),"no removal of a name not reported");
            check(mods::remove_failed("rom.z64.srw64mod") && !fs::exists(library/"rom.z64.srw64mod"),"a failed file goes");
            {
                std::error_code error;bool kept=false;
                for(const auto& item:fs::directory_iterator(library/".trash",error))kept|=item.path().filename().string().starts_with("rom.z64.srw64mod-");
                check(kept,"kept in .trash");
            }
            const auto after=mods::loose_results();
            check(std::none_of(after.begin(),after.end(),[](const auto& r){return r.file.filename()=="rom.z64.srw64mod";}),"and is no longer reported");
            list=mods::installed(game);
            check(ids(list).size()==before,"the packages untouched");
        }
        // A folder of an id takes the place of its file: an author's working copy.
        write(library/"test.loose"/"mod.json",manifest("test.loose","9.0.0").dump());
        list=mods::installed(game);
        check(find(list,"test.loose").manifest->version==mods::Version{9,0,0} && !find(list,"test.loose").archived(),"the folder over the file");
        check(mods::remove("test.loose") && !fs::exists(library/"test.loose") && !fs::exists(library/"test.loose.srw64mod"),"removing takes both");
        // A folder from before 0.5.4, its voices in voice.srw64mod: read as its own files.
        {
            write(library/"test.legacy"/"mod.json",manifest("test.legacy").dump());
            write(library/"test.legacy"/"audio"/"voice.json","{}");
            zip_file(library/"test.legacy"/mods::voice_archive,{{"pack/mod.json",manifest("test.legacy").dump()},{"pack/audio/voice/battle/5799.ogg","first"},
                                                                {"pack/audio/music/1.ogg","not a voice"}});
            list=mods::installed(game);
            const auto legacy=mods::files_of(find(list,"test.legacy"));
            check(find(list,"test.legacy").contents.voices==1 && legacy->list("audio/voice/battle")==std::vector<std::string>{"5799.ogg"},"its voices listed");
            check(text(legacy->read("audio/voice/battle/5799.ogg"))=="first" && !legacy->exists(mods::voice_archive) && !legacy->exists("audio/music/1.ogg"),
                  "read out of the file, nothing else of it");
            check(mods::remove("test.legacy") && !fs::exists(library/"test.legacy"),"it goes as a folder does");
        }
        list=mods::installed(game);
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
