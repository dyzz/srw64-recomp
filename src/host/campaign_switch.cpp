#include "campaign_switch.hpp"
#include "mini_stage.hpp"
#include "save_store.hpp"
#include "app/save_library.hpp"
#include "app/sram.hpp"
#include "librecomp/game.hpp"
#include <array>
#include <fstream>
#include <string_view>
#include <vector>
#include "ultramodern/ultramodern.hpp"
#include <cstdlib>
#include <filesystem>
#include <optional>

namespace srw64::campaign_switch {
namespace {
namespace fs=std::filesystem;
fs::path campaigns_root(){const char* root=std::getenv("SRW64_CAMPAIGN_SAVES");return root && *root?fs::path(root):fs::path();}
// <config>/saves, where librecomp keeps the game's SRAM file: the main game's file is
// <game>.bin there, a campaign's campaigns/<id>/<game>.bin. Read before the first switch.
fs::path runtime_saves() {
    static const fs::path folder=ultramodern::get_save_file_path().parent_path();
    return folder;
}
}

// A start in a campaign (--campaign) has no main game's library to come back to.
bool available() {
    const char* started_in=std::getenv("SRW64_CAMPAIGN");
    return !save_store::main_library().empty() && !campaigns_root().empty() && !(started_in && *started_in);
}

void enter(const campaign_library::Entry& entry) {
    if(!available())throw std::runtime_error("campaigns need a save library");
    if(!campaign_library::valid_id(entry.id))throw std::runtime_error("campaign id names no folder: "+entry.id);
    const auto folder=runtime_saves();   // before the save file moves
    const auto library=campaigns_root()/entry.id/"saves";
    fs::create_directories(library);
    const auto game=recomp::current_game_id();
    const auto main_file=folder/(fs::path(game).string()+".bin");
    // A campaign never played gets a formatted blank card: the game formats SRAM only when
    // it boots (8009171C), so a blank one would take saves the store cannot publish. The
    // header's options byte (stereo/mono) is the main game's.
    if(!fs::is_regular_file(library/"cartridge.sram")) {
        std::vector<uint8_t> card(app::sram::size,0);
        std::copy(app::sram::magic.begin(),app::sram::magic.end(),card.begin());
        std::array<char,8> header{};
        if(std::ifstream main(main_file,std::ios::binary);main && main.read(header.data(),header.size()) &&
           std::string_view(header.data(),app::sram::magic.size())==app::sram::magic)card[app::sram::magic.size()]=uint8_t(header.back());
        app::SaveLibrary(library).publish_cartridge(card);
    }
    // The campaign's SRAM starts as its library's card.
    const auto file=folder/"campaigns"/entry.id/(fs::path(game).string()+".bin");
    fs::create_directories(file.parent_path());
    std::error_code error;
    fs::copy_file(library/"cartridge.sram",file,fs::copy_options::overwrite_existing,error);
    if(error)throw std::runtime_error("cannot prepare the campaign's save: "+error.message());
    // The current SRAM is written out first (librecomp waits for its saving thread).
    ultramodern::change_save_file(u8"campaigns/"+std::u8string(entry.id.begin(),entry.id.end()),game);
    save_store::switch_library(library);
    mini_stage::load_file(entry.path,false);
}

void leave() {
    if(!available())throw std::runtime_error("campaigns need a save library");
    ultramodern::change_save_file(u8"",recomp::current_game_id());
    save_store::switch_library(save_store::main_library());
    mini_stage::clear_campaign();
}
}
