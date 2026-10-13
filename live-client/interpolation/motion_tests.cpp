#include "clientcommon/RemoteMotion.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace clientcommon;
int checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void near(float got,float expected){require(std::abs(got-expected)<.001f,"Unexpected interpolated value");}
RemoteMotionSample point(uint64_t t,uint32_t count,float x,float yaw=0,uint64_t parent=0,bool authoritative=false){return {{x,2,3,yaw},parent,t,count,authoritative};}
double dist(const RemotePose& a,const RemotePose& b){const double x=double(a.x)-b.x,y=double(a.y)-b.y,z=double(a.z)-b.z;return std::sqrt(x*x+y*y+z*z);}
void controls(){
    RemoteMotion m;require(!m.sample(0),"Uninitialized pose must be absent");
    require(m.update(point(0,1,0),0)==RemoteMotionUpdate::Initial,"First sample must initialize");near(m.sample(0)->x,0);
    require(m.update(point(400,2,1),400)==RemoteMotionUpdate::Retargeted,"Ordinary movement must interpolate");
    near(m.sample(400)->x,0);near(m.sample(600)->x,.5);near(m.sample(800)->x,1);near(m.sample(2000)->x,1);
    const auto before=*m.sample(650);m.update(point(650,3,2),650);near(m.sample(650)->x,before.x);
    near(m.sample(900)->x,2);near(m.sample(10000)->x,2);
    require(m.update(point(700,2,-5),1000)==RemoteMotionUpdate::Rejected,"Older counter cannot move a remote backward");
    require(m.update(point(700,3,-5),1000)==RemoteMotionUpdate::Rejected,"Duplicate counter cannot change the target");
    require(m.update(point(640,4,-5),1000)==RemoteMotionUpdate::Rejected,"Older arrival cannot replace a target");
    near(m.sample(1000)->x,2);
    require(m.update(point(1000,4,10,0,55),1000)==RemoteMotionUpdate::Snapped,"Parent transition must reset interpolation");near(m.sample(1000)->x,10);
    require(m.update(point(1400,5,12,0,55,true),1400)==RemoteMotionUpdate::Snapped,"Authoritative placement must apply immediately");near(m.sample(1400)->x,12);
    require(m.update(point(1800,6,100,0,55),1800)==RemoteMotionUpdate::Snapped,"Large teleport must not sweep through the world");near(m.sample(1800)->x,100);
    auto invalid=point(2000,7,0);invalid.pose.z=std::numeric_limits<float>::infinity();
    require(m.update(invalid,2000)==RemoteMotionUpdate::Rejected,"Nonfinite transform must be rejected");
    require(m.update(point(2500,7,0),2000)==RemoteMotionUpdate::Rejected,"Future arrival must be rejected");near(m.sample(2000)->x,100);
    RemoteMotion wrap;wrap.update(point(0,0xffffffffu,0,6.2f),0);wrap.update(point(400,0,1,.08f),400);
    near(wrap.sample(800)->x,1);require(std::abs(wrap.sample(600)->yaw)<.01f,"Heading must cross zero by the short arc");
    RemoteMotion stopped;stopped.update(point(0,1,0),0);stopped.update(point(400,2,1),400);
    require(stopped.update(point(600,3,1),600)==RemoteMotionUpdate::Unchanged,"Repeated endpoint must not extend settling");near(stopped.sample(800)->x,1);
    RemoteMotion jitter;jitter.update(point(0,1,0),0);jitter.update(point(800,2,2),800);
    near(jitter.sample(800)->x,0);near(jitter.sample(1200)->x,1);near(jitter.sample(1600)->x,2);
    jitter.update(point(9000,3,3),9000);near(jitter.sample(9000)->x,2);near(jitter.sample(9800)->x,3);
    RemoteMotion rapid;rapid.update(point(0,1,0),0);rapid.update(point(1,2,1),1);near(rapid.sample(101)->x,.5);near(rapid.sample(201)->x,1);
    // At realistic large world coordinates, endpoints are returned exactly,
    // without accumulating float integration error or overshooting.
    RemoteMotion large;auto a=point(0,1,-4831.25f);auto b=point(409,2,-4830.125f);
    large.update(a,0);large.update(b,409);require(large.sample(818)->x==b.pose.x,"Stop endpoint must be exact at world scale");
}
struct Record{uint64_t time,id,parent;uint32_t count;float x,y,z;};
void replay(const char*path){
    std::ifstream file(path);require(bool(file),"Cannot open recorded movement fixture");
    std::map<uint64_t,std::vector<Record>> objects;Record r;
    while(file>>r.time>>r.id>>r.parent>>r.count>>r.x>>r.y>>r.z)objects[r.id].push_back(r);
    require(!objects.empty(),"Recorded replay must not be empty");
    double rawMax=0,smoothMax=0,arrivalJump=0;size_t updates=0,frames=0,rejected=0;
    for(const auto&[id,points]:objects){
        RemoteMotion motion;size_t i=0;RemotePose raw{},previousRaw{},previousDraw{};bool initialized=false;
        const auto finish=points.back().time+1000;
        for(uint64_t time=points.front().time;time<=finish;time+=33){
            while(i<points.size()&&points[i].time<=time){
                const auto&p=points[i++];const auto before=motion.sample(time);
                const auto result=motion.update({{p.x,p.y,p.z,0},p.parent,p.time,p.count,false},time);
                if(result==RemoteMotionUpdate::Rejected){++rejected;continue;}
                raw={p.x,p.y,p.z,0};++updates;
                if(before&&result==RemoteMotionUpdate::Retargeted)arrivalJump=std::max(arrivalJump,dist(*before,*motion.sample(time)));
            }
            if(auto pose=motion.sample(time)){
                if(initialized){rawMax=std::max(rawMax,dist(raw,previousRaw));smoothMax=std::max(smoothMax,dist(*pose,previousDraw));}
                initialized=true;previousRaw=raw;previousDraw=*pose;++frames;
            }
        }
        require(dist(*motion.sample(finish),raw)==0,"Recorded trajectory must settle exactly at last accepted endpoint");
    }
    require(updates>1000&&frames>1000,"Replay must cover the actual multi-object trace");
    require(arrivalJump==0,"New normal packets must not cause a presentation jump");
    require(smoothMax<rawMax*.25,"Interpolation must reduce maximum frame displacement by at least 75 percent");
    std::cout<<"{\"recorded_replay\":true,\"objects\":"<<objects.size()<<",\"accepted_updates\":"<<updates
             <<",\"rejected_updates\":"<<rejected<<",\"frames\":"<<frames<<",\"raw_max_frame_step_m\":"<<rawMax
             <<",\"interpolated_max_frame_step_m\":"<<smoothMax<<",\"arrival_position_jump_m\":"<<arrivalJump<<"}\n";
}
int main(int argc,char**argv){try{controls();if(argc==2)replay(argv[1]);std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
