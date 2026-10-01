#include "sram.hpp"
#include <algorithm>
#include <stdexcept>

namespace srw64::app::sram {
namespace {
size_t stride(Order order){return order==Order::word?4:order==Order::half?2:1;}
// Reversing within each word or half is its own inverse.
Bytes reorder(std::span<const uint8_t> bytes,Order order) {
    Bytes out(bytes.begin(),bytes.end());
    if(const auto n=stride(order);n>1)
        for(size_t i=0;i+n<=out.size();i+=n)std::reverse(out.begin()+std::ptrdiff_t(i),out.begin()+std::ptrdiff_t(i+n));
    return out;
}
bool magic_at(std::span<const uint8_t> card,Order order) {
    const auto head=reorder(card.first(8),order);
    return std::equal(magic.begin(),magic.end(),head.begin());
}
std::span<const uint8_t> sram_of(std::span<const uint8_t> file,bool retroarch) {
    return retroarch?file.subspan(retroarch_sram,size):file;
}
}

std::optional<Detection> detect(std::span<const uint8_t> file) {
    for(const bool retroarch:{false,true}) {
        if(file.size()!=(retroarch?retroarch_size:size))continue;
        for(const auto order:{Order::big,Order::word,Order::half})
            if(magic_at(sram_of(file,retroarch),order))return Detection{order,retroarch};
    }
    return std::nullopt;
}
Bytes to_cartridge(std::span<const uint8_t> file) {
    const auto found=detect(file);
    if(!found)throw std::runtime_error("Not a Super Robot Wars 64 save: expected a 32 KiB SRAM file or a RetroArch .srm");
    return reorder(sram_of(file,found->retroarch),found->order);
}
Bytes from_cartridge(std::span<const uint8_t> cartridge,Format format,std::span<const uint8_t> container) {
    if(cartridge.size()!=size)throw std::runtime_error("A cartridge save is 32 KiB");
    const auto order=format==Format::ares?Order::big:Order::word;
    auto card=reorder(cartridge,order);
    if(format!=Format::retroarch)return card;
    if(!container.empty() && container.size()!=retroarch_size)throw std::runtime_error("Not a RetroArch mupen64plus .srm");
    Bytes out=container.empty()?Bytes(retroarch_size,0):Bytes(container.begin(),container.end());
    std::copy(card.begin(),card.end(),out.begin()+std::ptrdiff_t(retroarch_sram));
    return out;
}
std::optional<Format> format_for(std::string_view extension) {
    std::string lower(extension);
    for(auto& c:lower)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    if(lower==".ram" || lower==".sav")return Format::ares;
    if(lower==".sra")return Format::project64;
    if(lower==".srm")return Format::retroarch;
    return std::nullopt;
}
std::string_view name(Format format) {
    switch(format){case Format::ares:return "ares";case Format::project64:return "project64";
                   case Format::mupen64plus:return "mupen64plus";case Format::retroarch:return "retroarch";}
    return "?";
}
std::string_view name(Order order) {
    switch(order){case Order::big:return "big-endian";case Order::word:return "32-bit swapped";case Order::half:return "16-bit swapped";}
    return "?";
}

std::span<const uint8_t> slot(std::span<const uint8_t> cartridge,unsigned index){return cartridge.subspan(slot_offsets.at(index),slot_size);}
std::span<uint8_t> slot(std::span<uint8_t> cartridge,unsigned index){return cartridge.subspan(slot_offsets.at(index),slot_size);}
std::span<const uint8_t> suspend(std::span<const uint8_t> cartridge){return cartridge.subspan(suspend_offset,suspend_size);}
bool formatted(std::span<const uint8_t> cartridge){return cartridge.size()==size && magic_at(cartridge,Order::big);}
uint8_t checksum(std::span<const uint8_t> record) {
    unsigned sum=0;
    for(size_t i=2;i<2+slot_size && i<record.size();++i)sum+=record[i];
    return uint8_t(sum);
}
bool used(std::span<const uint8_t> record){return record.size()>=2 && (record[0]&0x80);}
bool intact(std::span<const uint8_t> record){return record.size()>=2 && record[1]==checksum(record);}
Summary summary(std::span<const uint8_t> record) {
    if(record.size()!=slot_size)throw std::runtime_error("A slot record is 0x1F00 bytes");
    const auto u16=[&](size_t at){return uint16_t(record[at]<<8|record[at+1]);};
    return {record[0x4F],record[0x51],u16(0x4C),uint32_t(u16(0x54))<<16|u16(0x56)};
}
std::vector<std::string> problems(std::span<const uint8_t> cartridge) {
    if(cartridge.size()!=size)return {"not 32 KiB"};
    if(!formatted(cartridge))return {"no SRW64V3 header"};
    std::vector<std::string> found;
    for(unsigned n=0;n<2;++n)
        if(used(slot(cartridge,n)) && !intact(slot(cartridge,n)))found.push_back("slot "+std::to_string(n+1)+" checksum");
    if(used(suspend(cartridge)) && !intact(suspend(cartridge)))found.push_back("suspend checksum");
    return found;
}
void merge_seen(std::span<uint8_t> into,std::span<const uint8_t> from) {
    for(size_t i=0;i<seen_size;++i)into[seen_offset+i]|=from[seen_offset+i];
}
}
