#include "critical_probability.hpp"
#include "defense_preview.hpp"
#include <cassert>
int main() {
    using namespace srw64::combat_preview;
    assert(critical_outcomes(critical_threshold(0,121,100,-20,0))==1);
    assert(critical_outcomes(critical_threshold(0,121,100,20,0))==41);
    assert(critical_outcomes(critical_threshold(0,121,100,20,40))==81);
    // Fractional thresholds count integer outcomes by ceiling, not truncation.
    assert(critical_outcomes(critical_threshold(1,121,100,0,90))==6);
    assert(critical_outcomes(critical_threshold(2,121,100,20,90))==11);
    assert(critical_outcomes(critical_threshold(0,10,200,-20,0))==1);
    assert(critical_outcomes(critical_threshold(0,200,0,30,90))==100);
    assert(critical_outcomes(90,true)==0);
    assert(defense_percent(1,0)==7 && defense_percent(1,1)==4);
    assert(defense_percent(9,0)==57 && defense_percent(9,2)==29);
    assert(barrier(2000,0x20,0,5,0,true,false).damage==0);
    assert(barrier(2001,0x20,0,5,0,true,false).damage==10);
    assert(barrier(4001,0x20,0,5,0,true,true).damage==10);
    assert(barrier(3201,0x4000,200,5,0,true,false).damage==3201);
    assert(barrier(3200,0x4000,200,5,0,true,false).damage==0);
    assert(barrier(1000,0x20,0,4,0,true,false).damage==1000);
    assert(barrier(1000,0x20,0,5,1,true,false).damage==1000);
    assert(barrier(1000,0x20,0,5,0,false,false).damage==1000);
    assert(shield_damage(39)==10 && shield_damage(40)==20);
    // Exhaust the discrete random domain for every legal signed-byte modifier,
    // both faction rules, caps and quarter-point thresholds.
    for(unsigned side=0;side<3;++side)for(int skill=-200;skill<=200;++skill)
    for(int bonus=-128;bonus<128;++bonus){
        const float t=critical_threshold(side,200+skill,200,bonus,30);
        int successes=0;for(int r=0;r<100;++r)successes+=r<t;
        assert(critical_outcomes(t)==successes);
    }
}
