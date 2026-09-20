#include "rom_import.hpp"
#include "rom_import_codec.hpp"
#include "portrait_png.hpp"
#include "sha256.hpp"
#include "json/json.hpp"
#include <charconv>
#include <random>
#include <regex>
#include <set>
#ifdef SRW64_EMBEDDED_IMPORT_SPEC
#include "srw64_import_spec.hpp"
#endif

namespace srw64::app {
std::string_view embedded_import_spec() {
#ifdef SRW64_EMBEDDED_IMPORT_SPEC
    return srw64_import_spec;
#else
    return {};
#endif
}
namespace {
using json=nlohmann::json;
using rom_import::Bytes;
constexpr unsigned importer_version=1;
std::string hash(Bytes data){Sha256 value;value.update(data);return value.finish();}
Bytes bytes(std::string_view text){return {reinterpret_cast<const uint8_t*>(text.data()),text.size()};}
std::string hash(std::string_view text){return hash(bytes(text));}
uint64_t integer(const json& value,uint64_t maximum,const char* label) {
    if(!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() && value.get<int64_t>()<0))
        throw std::runtime_error(std::string("Invalid integer: ")+label);
    const auto n=value.get<uint64_t>();if(n>maximum)throw std::runtime_error(std::string("Integer outside range: ")+label);return n;
}
json load(const fs::path& p,size_t limit=32*1024*1024){return json::parse(read_text(p,limit));}
// Same barrier/parameter signature as srw64_native.catalog.signature. Unicode
// text and BR are free; each STOP/END-separated segment owns its G parameters.
std::vector<std::vector<unsigned>> signature(const std::string& text) {
    if(!text.ends_with("<END>") || text.find_first_of(std::string("\0\r\n\f",4))!=std::string::npos)
        throw std::runtime_error("Invalid message control/terminator");
    static const std::regex token("<(BR|STOP|END|G:[0-9A-Fa-f]{1,4})>");
    std::vector<std::vector<unsigned>> result(1);size_t cursor=0;
    for(auto i=std::sregex_iterator(text.begin(),text.end(),token);i!=std::sregex_iterator();++i) {
        const auto start=size_t(i->position());
        if(text.substr(cursor,start-cursor).find('<')!=std::string::npos)throw std::runtime_error("Unsupported message token");
        const auto value=(*i)[1].str();
        if(value=="STOP" || value=="END"){result.back().push_back(value=="STOP"?0xfffd:0xffff);result.emplace_back();}
        else if(value.starts_with("G:"))result.back().push_back(unsigned(std::stoul(value.substr(2),nullptr,16)));
        cursor=start+size_t(i->length());
    }
    if(text.substr(cursor).find('<')!=std::string::npos)throw std::runtime_error("Unsupported message token");
    for(size_t i=0;i+2<result.size();++i)
        if(std::find(result[i].begin(),result[i].end(),0xffff)!=result[i].end())throw std::runtime_error("Early END in message");
    return result;
}
void verify_cached(const fs::path& path,const std::string& key,const std::string& rom_hash) {
    if(fs::is_symlink(path) || !fs::is_directory(path))throw std::runtime_error("Invalid import cache directory");
    const auto manifest=load(content_path(path,"manifest.json"),1024*1024);
    if(manifest.value("schema","")!="srw64.standalone-content.v1" || manifest.value("import_key","")!=key ||
       manifest.value("rom_sha256","")!=rom_hash || manifest.value("importer_version",0u)!=importer_version)
        throw std::runtime_error("Import cache identity/version mismatch");
    const auto& files=manifest.at("files");
    if(!files.is_object() || files.empty() || files.size()>2048 || !files.contains("dialogue.json"))
        throw std::runtime_error("Invalid import cache inventory");
    for(const auto& [name,digest]:files.items())
        if(sha256_file(content_path(path,name))!=digest.get<std::string>())throw std::runtime_error("Import cache file changed: "+name);
}
json compile_text(Bytes rom,const json& spec) {
    std::map<uint16_t,std::string> glyphs;
    for(const auto& [key,value]:spec.at("glyphs").items()) {
        unsigned id=0;const auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),id);
        if(error!=std::errc{} || end!=key.data()+key.size() || id>=0x8000 || !value.is_string() || value.get<std::string>().empty())
            throw std::runtime_error("Invalid glyph map");
        if(!glyphs.emplace(uint16_t(id),value.get<std::string>()).second)throw std::runtime_error("Duplicate glyph id");
    }
    const auto& layout=spec.at("text_layout");
    const auto records=rom_import::text_records(rom,size_t(integer(layout.at("pointer_table_offset"),rom.size(),"text table")),
        unsigned(integer(layout.at("table_count"),100,"table count")),unsigned(integer(layout.at("entry_header_size"),64,"text header")),glyphs);
    json sources=json::object(),hashes=json::object();
    for(const auto& row:records){sources[row.key]=row.text;hashes[row.key]=hash(rom_import::slice(rom,row.offset,row.size));}
    json catalogs=json::object(),options=json::array();
    const auto font_size=integer(spec.at("font_size"),18,"font size");
    if(font_size<10)throw std::runtime_error("Invalid font size");
    const auto& locales=spec.at("locales");
    if(!locales.is_array() || locales.empty() || locales.size()>32)throw std::runtime_error("Invalid locale registry");
    for(const auto& locale:locales) {
        const auto id=locale.at("locale").get<std::string>();
        if(catalogs.contains(id) || !std::regex_match(id,std::regex("[a-z]{2,3}(-[A-Za-z0-9]{2,8})*")))throw std::runtime_error("Invalid/duplicate locale");
        if(locale.at("schema")!="srw64.locale.v1" || locale.at("source_locale")!="ja" || locale.at("font").get<std::string>().empty())
            throw std::runtime_error("Invalid locale metadata");
        json entries=json::object();unsigned reviewed=0;
        for(const auto& row:locale.at("entries")) {
            const auto key=row.at("key").get<std::string>(),status=row.value("review_status","draft");
            if(status!="draft" && status!="reviewed")throw std::runtime_error("Invalid translation review status");
            if(!sources.contains(key) || entries.contains(key))throw std::runtime_error("Unknown/duplicate translation: "+key);
            if(row.at("source_sha256")!=hashes.at(key))throw std::runtime_error("Translation source changed: "+key);
            const auto target=row.at("target").get<std::string>();
            if(signature(target)!=signature(sources.at(key).get<std::string>()))throw std::runtime_error("Translation changes script barriers: "+key);
            entries[key]=target;if(status=="reviewed")++reviewed;
        }
        catalogs[id]={{"config",{{"font",locale.at("font")},{"locale",id},{"font_size",font_size},{"mode","replace"}}},
                      {"entries",entries},{"ui",locale.at("ui")},{"catalog_sha256",locale.at("catalog_sha256")}};
        options.push_back({{"locale",id},{"label",locale.value("display_name",id)},{"translated",entries.size()},
                           {"source",records.size()},{"reviewed",reviewed}});
    }
    const auto selected=spec.at("locale").get<std::string>();
    if(!catalogs.contains("ja") || !catalogs.contains(selected))throw std::runtime_error("Japanese fallback or selected locale missing");
    json data=catalogs.at(selected);
    data["schema"]="srw64.native-dialogue-data.v2";data["rom_sha256"]=spec.at("rom_sha256");
    data["source_entries"]=sources;data["glyphs"]=spec.at("glyphs");
    data["locale_options"]=options;data["locale_catalogs"]=catalogs;
    return data;
}
}
fs::path prepare_rom_content(const fs::path& rom_path,const fs::path& cache_root,
                             std::string_view spec_text,const std::string& expected_rom_hash) {
    if(spec_text.empty())throw std::runtime_error("This build has no embedded importer metadata; rebuild with SRW64_EMBED_IMPORT_SPEC or pass --content");
    if(spec_text.size()>8*1024*1024)throw std::runtime_error("Import metadata exceeds budget");
    const auto spec=json::parse(spec_text);
    if(spec.value("schema","")!="srw64.native-import-spec.v1" || spec.at("baseline")!="srw64-jp-rev0" ||
       spec.at("rom_sha256")!=expected_rom_hash)throw std::runtime_error("Import metadata does not match the game");
    const size_t size=size_t(integer(spec.at("rom_size"),32*1024*1024,"ROM size"));
    if(!size || fs::file_size(rom_path)!=size)throw std::runtime_error("Incorrect ROM size");
    const auto raw=read_text(rom_path,size);const auto rom=bytes(raw);
    // Decode and hash the SAME immutable bytes, not a second unverified read.
    if(raw.size()!=size || hash(rom)!=expected_rom_hash)throw std::runtime_error("ROM identity check failed before import");
    const auto key=hash(expected_rom_hash+":"+std::to_string(importer_version)+":"+hash(spec_text));
    if(fs::is_symlink(cache_root))throw std::runtime_error("Refusing a symlinked import cache");
    fs::create_directories(cache_root);
    const auto destination=cache_root/key;
    if(fs::exists(destination) || fs::is_symlink(destination)) {
        try{verify_cached(destination,key,expected_rom_hash);}
        catch(const std::exception& e){throw std::runtime_error("Cached import is invalid; remove only "+destination.string()+" to rebuild: "+e.what());}
        return destination;
    }
    fs::path staging;
    for(unsigned attempt=0;attempt<16;++attempt) {
        const auto candidate=cache_root/(key+".tmp-"+std::to_string(std::random_device{}()));
        if(fs::create_directory(candidate)){staging=candidate;break;}
    }
    if(staging.empty())throw std::runtime_error("Cannot create import staging directory");
    try {
        std::fprintf(stderr,"SRW64_ROM_IMPORT_BEGIN %s\n",destination.string().c_str());
        auto data=compile_text(rom,spec);json inventory=json::object(),portraits=json::object();
        fs::create_directory(staging/"name-entry");
        auto publish=[&](const std::string& relative,Bytes content) {
            atomic_write(staging/relative,{reinterpret_cast<const char*>(content.data()),content.size()});
            inventory[relative]=hash(content);
        };
        constexpr size_t route_offset=0x1090a0+0x801c6bf0-0x801c2600;
        constexpr std::array<unsigned,8> routes{27,28,25,26,31,32,29,30};
        constexpr std::array<unsigned,8> linked{41,230,133,131,132,152,151,153};
        for(size_t i=0;i<routes.size();++i)
            if(rom_import::be16(rom,route_offset+i*2)!=routes[i])throw std::runtime_error("Opening portrait table changed");
        std::set<unsigned> faces(routes.begin(),routes.end());faces.insert(linked.begin(),linked.end());
        for(const auto face:faces) {
            const auto image_id=rom_import::be16(rom,0x84220+size_t(face)*4),palette_id=rom_import::be16(rom,0x84222+size_t(face)*4);
            const auto image=rom_import::resource(rom,0xa20bd0,image_id),palette=rom_import::resource(rom,0xa20bd0,palette_id);
            const auto pixels=rom_import::portrait(image.bytes,palette.bytes);
            const auto png=rom_import::portrait_png(pixels);
            const auto relative="name-entry/face-"+std::to_string(face)+".png";publish(relative,png);
            portraits[std::to_string(face)]={{"original",relative},{"original_sha256",inventory.at(relative)},
                {"resource_id",image_id},{"palette_id",palette_id}};
        }
        data["name_entry_assets"]={{"schema","srw64.name-entry-assets.v1"},{"portraits",portraits},
            {"route_faces",{{27,28,25,26},{31,32,29,30}}},{"link_faces",{{41,230},{133,131,132},{152,151,153}}},{"source_sha256",nullptr}};
        const auto dialogue=data.dump()+"\n";
        if(dialogue.size()>32*1024*1024)throw std::runtime_error("Dialogue output exceeds bootstrap budget");
        publish("dialogue.json",bytes(dialogue));
        const auto scale=integer(spec.at("resolution_scale"),8,"resolution scale");if(!scale)throw std::runtime_error("Invalid resolution scale");
        const json manifest={{"schema","srw64.standalone-content.v1"},{"baseline","srw64-jp-rev0"},{"rom_sha256",expected_rom_hash},
            {"dialogue","dialogue.json"},{"resolution_scale",scale},{"files",inventory},{"distribution","local-only-rom-derived-content"},
            {"importer_version",importer_version},{"import_key",key},{"spec_sha256",hash(spec_text)}};
        atomic_write(staging/"manifest.json",manifest.dump(2)+"\n");verify_cached(staging,key,expected_rom_hash);
        // The caller holds the Session lock; no reader observes a partial cache.
        fs::rename(staging,destination);
        std::fprintf(stderr,"SRW64_ROM_IMPORT_COMPLETE %zu text records, %zu portraits\n",data.at("source_entries").size(),portraits.size());
        return destination;
    } catch(...) {std::error_code ignored;fs::remove_all(staging,ignored);throw;}
}
}
