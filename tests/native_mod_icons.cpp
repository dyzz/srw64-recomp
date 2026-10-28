// MOD map icons (src/host/unit_icon_art.hpp, docs/design/mod-names-art-data.md §2.3): the
// sizes taken, the master each picture scales to, the sides' variants, and the quad read
// back from the drawer's texture rectangle, as drawn and as turned by unit_icon_facing.
#include "unit_icon_art.hpp"
#include <cmath>
#include <cstdio>
#include <string>
using namespace srw64;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}
static const uint8_t* at(const std::vector<uint8_t>& v,int x,int y){return &v[(size_t(y)*unit_icon_art::kSize+x)*4];}
static bool near(float a,float b){return std::fabs(a-b)<1e-4f;}

int main() {
    check(unit_icon_art::size_ok(16,16) && unit_icon_art::size_ok(128,128),"16 and 128 are taken");
    check(!unit_icon_art::size_ok(96,96) && !unit_icon_art::size_ok(256,256) && !unit_icon_art::size_ok(64,32),"96, 256 and oblong are not");

    // A 16x16 picture: one opaque pixel at (5,7), the rest transparent; each pixel becomes 8x8.
    std::vector<uint8_t> small(16*16*4,0);
    small[(7*16+5)*4+0]=200;small[(7*16+5)*4+1]=100;small[(7*16+5)*4+2]=50;small[(7*16+5)*4+3]=255;
    const auto master=unit_icon_art::master(small.data(),16);
    check(master.size()==size_t(128*128*4),"the master is 128x128");
    check(at(master,40,56)[0]==200 && at(master,47,63)[3]==255 && at(master,48,56)[3]==0 && at(master,39,63)[3]==0,"each pixel fills its 8x8 block");

    // The player's variant is the master; the enemy's has a bright ring and a dark one outside it.
    check(unit_icon_art::marked(master,0)==master,"the player's icon is as drawn");
    const auto enemy=unit_icon_art::marked(master,1);
    check(at(enemy,40,56)[0]==200,"the sprite keeps its colours");
    check(at(enemy,39,58)[0]==222 && at(enemy,39,58)[1]==123 && at(enemy,39,58)[3]==255,"one bright pixel beside it");
    check(at(enemy,38,58)[0]==156 && at(enemy,38,58)[1]==25,"one dark pixel beyond");
    check(at(enemy,37,58)[3]==0 && at(enemy,39,55)[0]==156 && at(enemy,39,54)[3]==0,"nothing further; the corner is dark only");
    const auto third=unit_icon_art::marked(master,2);
    check(at(third,48,60)[0]==222 && at(third,48,60)[1]==206 && at(third,49,60)[0]==140,"the third party's tones");
    // Acted: grey at 85%, transparency kept.
    const auto acted=unit_icon_art::marked(master,3);
    const int grey=int((200*0.299f+100*0.587f+50*0.114f)*0.85f);
    check(at(acted,40,56)[0]==grey && at(acted,40,56)[1]==grey && at(acted,40,56)[2]==grey && at(acted,40,56)[3]==255,"acted turns grey");
    check(at(acted,0,0)[3]==0,"acted keeps the transparency");

    // Premultiplied levels down to 1x1.
    const auto levels=unit_icon_art::levels(enemy);
    check(levels.size()==8 && levels.back().size()==4,"eight levels from 128 to 1");

    // The rectangle as drawn: 16x16 at (100,60), S/T from 0, one texel per pixel.
    unit_icon_art::Quad q{};
    const uint32_t w0=0xE4000000|(116*4)<<12|(76*4),w1=(100*4)<<12|(60*4),steps=0x04000400;
    check(unit_icon_art::quad(w0,w1,0,steps,q),"a rectangle reads");
    check(near(q.rect[0],100) && near(q.rect[3],76) && near(q.uv[0],0) && near(q.uv[1],0) && near(q.uv[2],1) && near(q.uv[3],1),"the whole icon");
    // Turned as unit_icon_facing::after_draw writes it: S at the far edge less a 32nd, DsDx negative.
    const uint32_t st=uint32_t(16*32-1)<<16,turned=uint32_t(uint16_t(int16_t(-0x400)))<<16|0x400;
    check(unit_icon_art::quad(w0,w1,st,turned,q) && near(q.uv[0],1) && near(q.uv[2],0) && near(q.uv[3],1),"mirrored");
    // The whole-map view draws it at half size: two texels per pixel.
    const uint32_t half=0xE4000000|(108*4)<<12|(68*4);
    check(unit_icon_art::quad(half,w1,0,0x08000800,q) && near(q.uv[2],1) && near(q.rect[2],108),"scaled down");
    check(!unit_icon_art::quad(w1|0xE4000000,w0&0xFFFFFF,0,steps,q),"an empty rectangle is refused");

    if(failures)return 1;
    std::printf("mod icons: ok\n");
    return 0;
}
