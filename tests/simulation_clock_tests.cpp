#include "simulation_clock.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    for(float scale:{.3f,1.f,2.f})for(int fps:{30,60,120,144,165,240,500,1000,10000}){
        fnv::physics::SimulationClock clock;const float quantum=.016f*scale;
        const float frame=scale/fps;double input{},simulated{};unsigned idle{};
        for(int i=0;i<fps*30;++i){const auto step=clock.Consume(frame,quantum);
            CHECK(step.count<=3 && (step.count==0)==(step.seconds==0));
            input+=frame;simulated+=double(step.count)*double(quantum);if(!step.count)++idle;
            CHECK(std::abs(simulated+clock.Remainder()-input)<1e-8);
        }
        CHECK(input-simulated>=-1e-9 && input-simulated<double(quantum)+1e-7);
        if(fps>=120)CHECK(idle>0);
    }
    fnv::physics::SimulationClock clock;
    for(float dt:{.003f,.009f,.005f,.024f,.001f,.012f})CHECK(clock.Consume(dt,.016f).count<=3);
    clock.Reset();CHECK(clock.Remainder()==0 && clock.Consume(.001f,.016f).count==0);
    CHECK(clock.Consume(0,.016f).count==0 && clock.Remainder()==0);
    CHECK(clock.Consume(std::numeric_limits<float>::quiet_NaN(),.016f).count==0);
    for(int n=0;n<10000;++n){CHECK(clock.Consume(10,.016f).count==3);CHECK(clock.Remainder()<=.0480001);}
    float delta=10,input=10,step=10,remainder=10;uint32_t count=10;
    const fnv::physics::SimulationStage stage{&delta,&input,&step,&remainder,&count};
    fnv::physics::PublishSimulationStep(stage,{0,0},.016f);CHECK(delta==0 && input==0 && step==0 && remainder==0 && count==0);
    fnv::physics::PublishSimulationStep(stage,{.032f,2},.016f);CHECK(delta==.032f && input==.032f && step==.016f && remainder==0 && count==2);
    std::puts("PASS fixed native quantum with uncapped render: 30..10000 FPS, time scales, conserved remainder, pause/reset, bounded stalls and idle controller signals");
}
