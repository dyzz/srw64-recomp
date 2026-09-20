#pragma once
// Small streaming SHA-256 implementation for local ROM/save/content integrity.
// This is a digest, not a signature or a trust decision about third-party packs.
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <stdexcept>

namespace srw64::app {
class Sha256 {
    static constexpr std::array<uint32_t, 64> k = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<uint32_t, 8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<uint8_t, 64> block{};
    size_t used{};
    uint64_t bytes{};
    static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void compress() {
        std::array<uint32_t, 64> w{};
        for (size_t i=0; i<16; ++i)
            w[i]=(uint32_t(block[4*i])<<24)|(uint32_t(block[4*i+1])<<16)|
                 (uint32_t(block[4*i+2])<<8)|block[4*i+3];
        for (size_t i=16; i<64; ++i) {
            const auto a=w[i-15], b=w[i-2];
            w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
        }
        auto [a,b,c,d,e,f,g,h]=state;
        for (size_t i=0; i<64; ++i) {
            const uint32_t t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
public:
    void update(std::span<const uint8_t> data) {
        bytes+=data.size();
        for (auto byte:data) { block[used++]=byte; if (used==64) { compress(); used=0; } }
    }
    std::string finish() const {
        auto copy=*this;
        const uint64_t bits=bytes*8;
        const uint8_t first=0x80, zero=0;
        copy.update({&first,1});
        while (copy.used!=56) copy.update({&zero,1});
        std::array<uint8_t,8> size{};
        for (unsigned i=0;i<8;++i) size[7-i]=uint8_t(bits>>(i*8));
        copy.update(size);
        std::ostringstream text; text<<std::hex<<std::setfill('0');
        for (auto value:copy.state) text<<std::setw(8)<<value;
        return text.str();
    }
};
inline std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read file for SHA-256: "+path.string());
    Sha256 hash;
    std::array<uint8_t,65536> buffer{};
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
        hash.update({buffer.data(),static_cast<size_t>(file.gcount())});
    }
    if (!file.eof()) throw std::runtime_error("Failed while hashing: "+path.string());
    return hash.finish();
}
}
