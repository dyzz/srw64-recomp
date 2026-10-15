// The stage info page's evaluator (src/host/stage_info.hpp) against the rules shipped in
// content/stages/status.json, on the saves tests/test_stage_info.py walks with the
// reference evaluator; and the page's line breaking (src/native/ui/text_wrap.hpp).
#include "stage_info.hpp"
#include "ui/text_wrap.hpp"
#include <cstdio>
#include <fstream>
#include <map>

using namespace srw64;
using json=nlohmann::json;

static int failures=0;
static void check(bool ok,const std::string& what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what.c_str());++failures;}}

static json rule(const stage_info::Rules& r,const std::string& id) {
    for(const auto& x:r.rules)if(x.at("id")==id)return x;
    return json();
}
static json eval(const stage_info::Rules& r,const std::string& id,const char* who,int scene,std::map<int,int> vars={},
                 std::set<int> pilots={},std::set<int> units={},std::map<int,int> kills={}) {
    stage_info::State s;s.protagonist=who;s.scene=scene;
    for(const auto& [n,v]:vars)s.vars[size_t(n)]=uint8_t(v);
    s.pilots=pilots;s.units=units;s.kills=kills;
    return stage_info::Evaluator(r,s).evaluate(rule(r,id));
}

int main(int argc,char** argv) {
    std::ifstream in(argc>1?argv[1]:"content/stages/status.json");
    const auto rules=stage_info::Rules::parse(json::parse(in));

    // The graph: the dawn operation splits; OZ never reaches the Independence Army's branch stage.
    {
        stage_info::State s;s.protagonist="brad";s.scene=47;
        check(!stage_info::Evaluator(rules,s).reachable(41),"S47 does not reach S41");
        s.scene=41;check(stage_info::Evaluator(rules,s).reachable(124),"Brad's S41 reaches S124");
        s.protagonist="ark";check(!stage_info::Evaluator(rules,s).reachable(124),"Ark's S41 does not reach S124");
    }
    // The side branch returns to its own route.
    check(eval(rules,"secret-four","ark",123,{{10,0},{11,0},{2,2}})["state"]=="missed","Four missed on IA after S123");
    check(eval(rules,"secret-four","ark",123,{{10,1},{2,2}})["state"]=="step","Four still open on OZ after S123");
    check(eval(rules,"secret-allenby","brad",124,{{10,0},{11,0},{3,0}})["state"]=="step","Allenby can be won back in S129");
    // Kurtz.
    check(eval(rules,"secret-katz","brad",25)["state"]=="not_started","Kurtz not started");
    check(eval(rules,"secret-katz","ark",25)["state"]=="na","Kurtz is Brad's");
    check(eval(rules,"secret-katz","brad",42,{{10,0},{11,0},{20,0},{21,3}})["state"]=="missed","Kurtz missed after the branch");
    {
        const auto settle=eval(rules,"secret-katz","brad",124,{{10,0},{20,1},{21,0}},{},{},{{27,80}});
        check(settle["state"]=="step" && settle["step"]==2 && settle["counter"]["value"]==80 && settle["counter"]["of"]==100,"Kurtz's settling stage counts kills");
    }
    check(eval(rules,"secret-katz","brad",42,{{21,2}},{31})["state"]=="done","Kurtz done");
    check(eval(rules,"secret-katz","brad",65,{{10,0},{11,1}})["state"]=="na","Kurtz not on complete pacifism");
    // Garalia's window.
    check(eval(rules,"secret-garalia","ark",20)["state"]=="not_started","Garalia open in S20");
    check(eval(rules,"secret-garalia","ark",21)["state"]=="missed","Garalia missed after S20");
    check(eval(rules,"secret-garalia","ark",21,{{4,0}})["state"]=="done","Garalia done");
    // Paths per route.
    check(eval(rules,"secret-reese","serein",25)["of"]==3,"Rish before the split: the Independence Army's three steps");
    {
        const auto oz=eval(rules,"secret-reese","serein",49,{{10,1}});
        check(oz["state"]=="not_started" && oz["of"]==2,"Rish on OZ: two steps");
    }
    check(eval(rules,"secret-reese","serein",36,{{10,0},{11,0},{14,0}})["state"]=="missed","Rish missed after the wrong answer");
    // Choices.
    check(eval(rules,"secret-route-oz","ark",30)["state"]=="pending","the dawn operation pending");
    check(eval(rules,"secret-route-oz","ark",31,{{10,0}})["label"]["en"]=="Independence Army route","the Independence Army chosen");
    check(eval(rules,"secret-route-treize","ark",74,{{10,0},{11,1}})["label"]["en"]=="With Treize","with Treize");
    check(eval(rules,"secret-route-treize","ark",72,{{10,0},{11,1}})["label"]["en"]=="On your own","on your own");
    check(eval(rules,"secret-route-beast-base","ark",81,{{10,0}})["label"]["en"]=="Stayed with the main force","the beast base's first answer");
    check(eval(rules,"secret-route-gato-base","ark",108,{{10,1},{37,3}})["label"]["en"]=="Base lost","the base lost");
    // Counts.
    {
        const auto trio=eval(rules,"secret-rx-trio","ark",15,{},{},{55,58});
        check(trio["state"]=="count" && trio["value"]==2 && trio["of"]==3,"two of the RX trio");
    }
    // No element is missed at a protagonist's first stage.
    for(const auto& [who,start]:std::map<std::string,int>{{"ark",2},{"serein",3},{"brad",0},{"manami",1}})
        for(const auto& r:rules.rules)
            check(eval(rules,r.at("id"),who.c_str(),start)["state"]!="missed",who+" "+r.at("id").get<std::string>()+" missed at the start");

    // Line breaking, with every character one unit wide.
    const auto width=[](const std::string& s){size_t n=0;for(size_t i=0;i<s.size();){text_wrap::decode(s,i);++n;}return float(n);};
    {
        // CJK breaks anywhere; the full stop stays on the line it follows.
        const auto lines=text_wrap::lines("用翔说服陶德。陶德与德雷克同组",6,width);
        check(lines==std::vector<std::string>{"用翔说服陶德。","陶德与德雷克","同组"},"CJK lines and closing punctuation");
    }
    {
        const auto lines=text_wrap::lines("Shoot down Kurtz with Brad",12,width);
        check(lines.size()==3 && lines[0]=="Shoot down" && lines[1]=="Kurtz with" && lines[2]=="Brad","Latin breaks between words");
    }
    {
        const auto lines=text_wrap::lines("在在在「了」",4,width);
        check(lines==std::vector<std::string>{"在在在","「了」"},"an opening bracket goes down with its word");
    }
    {
        // A curly opening quote goes down with the title it opens (English pages).
        const auto lines=text_wrap::lines("at the start of \u201CShowdown!\u201D",16,width);
        check(lines.size()==2 && lines[1].rfind("\u201CShowdown",0)==0,"an opening quote goes down with its word");
    }
    {
        const auto lines=text_wrap::lines("we don\u2019t know",5,width);
        check(lines==std::vector<std::string>{"we","don\u2019t","know"},"an apostrophe stays inside its word");
        // In Chinese an opening quote still goes down with the line it opens.
        const auto quoted=text_wrap::lines("翔说\u201C喂\u201D",3,width);
        check(quoted==std::vector<std::string>{"翔说","\u201C喂\u201D"},"a Chinese opening quote goes down");
    }
    if(failures)std::fprintf(stderr,"%d failed\n",failures);
    else std::puts("stage info: ok");
    return failures?1:0;
}
