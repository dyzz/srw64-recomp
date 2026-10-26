// MOD music (src/host/music.hpp, docs/design/audio-mods.md): music.json, which package's
// file wins for a song, and how a decoded track is mixed into the game's audio.
#include "music.hpp"
#include <cstdio>
#include <fstream>

using namespace srw64;
using json=nlohmann::json;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}

static void touch(const std::filesystem::path& path,const std::string& text="x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
static json read_json(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in)return json();
    auto data=json::parse(in,nullptr,false);
    return data.is_discarded()?json():data;
}
static mods::Package package(const std::filesystem::path& folder,bool active=true) {
    mods::Package p;p.folder=folder;p.active=active;
    mods::Manifest m;m.id=folder.filename().string();p.manifest=m;
    return p;
}
// A track of `frames` frames: left = index, right = -index.
static std::shared_ptr<music::Pcm> ramp(unsigned frames,unsigned rate=44100) {
    auto pcm=std::make_shared<music::Pcm>();pcm->rate=rate;
    for(unsigned i=0;i<frames;++i){pcm->samples.push_back(int16_t(i));pcm->samples.push_back(int16_t(-int(i)));}
    return pcm;
}

int main(int argc,char** argv) {
    // music.json.
    {
        const auto s=music::settings(json::parse(R"({"schema":"srw64.music.v1","tracks":{
            "7":{"loop":[100,2000],"gain":-6,"title":{"en":"Main"}},"38":{"loop":false},"x":{},"9":{"loop":[5,5]}}})"));
        check(s.size()==3 && s.at(7).loop==std::pair<uint64_t,uint64_t>{100,2000} && s.at(7).gain_db==-6 && s.at(7).title["en"]=="Main","a track's settings");
        check(s.at(38).once==true && !s.at(9).loop,"loop false plays once; an empty loop is ignored");
        check(music::settings(json::parse(R"({"schema":"other","tracks":{"7":{}}})")).empty(),"another schema reads nothing");
    }
    // Which file wins: the package higher in the list (later), active ones only.
    {
        namespace fs=std::filesystem;
        const fs::path root=argc>1?fs::path(argv[1]):fs::temp_directory_path()/"srw64-music-test";
        fs::remove_all(root);
        touch(root/"low"/"audio"/"music"/"7.ogg");touch(root/"low"/"audio"/"music"/"12.ogg");touch(root/"low"/"audio"/"music"/"intro.ogg");
        touch(root/"low"/"audio"/"music"/"300.ogg");touch(root/"low"/"audio"/"music"/"13.mp3");
        touch(root/"high"/"audio"/"music"/"7.OGG");
        touch(root/"high"/"audio"/"music.json",R"({"schema":"srw64.music.v1","tracks":{"7":{"title":"New main","loop":[1,9]}}})");
        touch(root/"off"/"audio"/"music"/"12.ogg");
        const auto table=music::collect({package(root/"low"),package(root/"high"),package(root/"off",false)},read_json);
        check(table.size()==2,"two songs: names that are not song numbers, other formats and turned-off packages are left out");
        check(table.at(7).package=="high" && table.at(7).title=="New main" && table.at(7).loop==std::pair<uint64_t,uint64_t>{1,9},"the higher package wins, with its settings");
        check(table.at(12).package=="low","a song only one package has");
        fs::remove_all(root);
    }
    // Who fights to which song: music.json "battle", the higher package winning per entry,
    // looked up by the ROM address 801E07C4 reads.
    {
        const auto b=music::battle_settings(json::parse(R"({"schema":"srw64.music.v1","battle":{
            "pilot":{"36":7,"999":3,"4":0,"x":2,"70":"7"},"unit":{"50":12,"362":4}}})"));
        check(b.pilot.size()==1 && b.pilot.at(36)==7,"pilots: out of range, song 0, bad keys and strings are left out");
        check(b.unit.size()==2 && b.unit.at(50)==12 && b.unit.at(362)==4,"units, the last one included");
        namespace fs=std::filesystem;
        const fs::path root=(argc>1?fs::path(argv[1]):fs::temp_directory_path()/"srw64-music-test")/"battle";
        fs::remove_all(root);
        touch(root/"low"/"audio"/"music.json",R"({"schema":"srw64.music.v1","battle":{"pilot":{"36":7,"4":5}}})");
        touch(root/"high"/"audio"/"music.json",R"({"schema":"srw64.music.v1","battle":{"pilot":{"36":9},"unit":{"97":20}}})");
        touch(root/"off"/"audio"/"music.json",R"({"schema":"srw64.music.v1","battle":{"pilot":{"4":1}}})");
        const auto songs=music::collect_battle({package(root/"low"),package(root/"high"),package(root/"off",false)},read_json);
        check(songs.pilot.at(36)==9 && songs.pilot.at(4)==5 && songs.unit.at(97)==20,"the higher package wins per pilot; a turned-off package counts for nothing");
        check(music::battle_song(songs,music::pilot_songs+2*36)==9,"Amuro's entry in the pilot table");
        check(music::battle_song(songs,music::unit_songs+2*97)==20,"the Zaku II's entry in the unit table");
        check(!music::battle_song(songs,music::pilot_songs+2*37) && !music::battle_song(songs,music::pilot_songs+2*36+1)
              && !music::battle_song(songs,music::unit_songs+2*music::unit_count),"no entry, an odd address or past the table: the original's song");
        fs::remove_all(root);
    }
    // Mixing at the track's own rate: the samples arrive as they are, added to the game's.
    {
        music::Voice v;v.pcm=ramp(1000);
        std::vector<int16_t> out(2*4,10);
        check(music::mix(v,out.data(),4,44100,false),"mixing goes on");
        check(out[0]==10 && out[2]==11 && out[3]==9 && out[6]==13 && out[7]==7,"added to the game's samples");
        check(v.position==4,"the position moves a frame per frame");
    }
    // Resampling: a 22050 Hz track into 44100 Hz output moves half a frame per frame.
    {
        music::Voice v;v.pcm=ramp(100,22050);
        std::vector<int16_t> out(2*3,0);
        music::mix(v,out.data(),3,44100,false);
        check(out[0]==0 && out[2]==0 && out[4]==1,"halfway samples interpolate (0, 0.5, 1)");
        check(v.position==1.5,"half a frame per output frame");
    }
    // Looping: the whole track, then a loop range.
    {
        music::Voice v;v.pcm=ramp(4);v.position=3;
        std::vector<int16_t> out(2*2,0);
        music::mix(v,out.data(),2,44100,false);
        check(out[2]==0 && !v.finished,"after the last frame the track starts again");
        music::Voice r;r.pcm=ramp(10);r.loop_start=2;r.loop_end=6;r.position=5;
        std::vector<int16_t> o2(2*2,0);
        music::mix(r,o2.data(),2,44100,false);
        check(o2[0]==5 && o2[2]==2,"a loop range jumps back to its start");
    }
    // Once: the jingle ends and says so.
    {
        music::Voice v;v.pcm=ramp(3);v.once=true;
        std::vector<int16_t> out(2*5,0);
        check(!music::mix(v,out.data(),5,44100,false) && v.finished,"a track played once ends");
        check(out[4]==2 && out[6]==0,"nothing after its end");
    }
    // Fading, gain, mono and clamping.
    {
        music::Voice v;v.pcm=ramp(100);v.position=50;v.fade_total=v.fade_left=2;
        std::vector<int16_t> out(2*3,0);
        check(!music::mix(v,out.data(),3,44100,false) && v.finished,"a fade ends the track");
        check(out[0]==50 && out[2]==25 && out[4]==0,"the fade ramps down");
        check(std::abs(music::gain_of(-6.0206)-0.5)<1e-4 && music::gain_of(0)==1,"-6 dB is half");
        music::Voice g;g.pcm=ramp(100);g.position=40;g.gain=0.5;
        std::vector<int16_t> o2(2,0);
        music::mix(g,o2.data(),1,44100,false);
        check(o2[0]==20,"the gain scales the track");
        music::Voice m;m.pcm=ramp(100);m.position=10;
        std::vector<int16_t> o3(2,0);
        music::mix(m,o3.data(),1,44100,true);
        check(o3[0]==0 && o3[1]==0,"mono averages the channels");
        auto loud=std::make_shared<music::Pcm>();loud->samples={30000,-30000,30000,-30000};
        music::Voice c;c.pcm=loud;
        std::vector<int16_t> o4={10000,-10000};
        music::mix(c,o4.data(),1,44100,false);
        check(o4[0]==32767 && o4[1]==-32768,"clamped, not wrapped");
    }
    // Still decoding: silence, the place kept.
    {
        music::Voice v;
        std::vector<int16_t> out(2,7);
        check(music::mix(v,out.data(),1,44100,false) && out[0]==7 && v.position==0,"silence while decoding");
    }
    if(failures)std::fprintf(stderr,"%d failed\n",failures);
    else std::puts("music: ok");
    return failures?1:0;
}
