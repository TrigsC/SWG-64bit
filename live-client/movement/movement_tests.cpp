#include "clientcommon/MovementReporting.h"
#include "soe/PacketBuffer.h"
#include "soe/MessageHash.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace clientcommon;
int checks=0;
void check(bool v){++checks;if(!v)throw std::runtime_error("Movement regression check "+std::to_string(checks));}
bool near(float a,float b){return std::abs(a-b)<1e-5f;}
int main(){
 try {
 SelfCorrectionTracker tracker;
 check(tracker.observe(1,0,true,true)==CorrectionDecision::KeepPrediction); // scene-create seed
 check(tracker.observe(2,1,true,false)==CorrectionDecision::KeepPrediction); // unresolved cell
 check(tracker.observe(2,1,true,true)==CorrectionDecision::ApplyAuthoritative);
 check(tracker.observe(2,1,true,true)==CorrectionDecision::KeepPrediction); // no repeated snapping
 check(tracker.observe(3,2,false,true)==CorrectionDecision::IgnoreStaleSnapshot);
 check(tracker.observe(3,2,false,true)==CorrectionDecision::IgnoreStaleSnapshot); // stale parent stays suppressed
 check(tracker.observe(4,2,false,true)==CorrectionDecision::KeepPrediction); // later ordinary broadcast
 check(tracker.observe(5,3,true,true)==CorrectionDecision::ApplyAuthoritative); // fresh same-cell relocation
 check(tracker.observe(5,3,true,true)==CorrectionDecision::KeepPrediction);
 MovementFrame f{42,0,100,2,-300,0,1.55f};MovementReporter reporter(1000);
 check(!reporter.poll(1399,f));auto first=reporter.poll(1400,f);check(first&&first->counter==1&&first->timestamp==400);
 check(!reporter.poll(1599,f));check(!reporter.poll(1799,f));
 // A 50ms tap after the previous report stops before the next network tick.
 f.z+=.0775f;f.speed=0;auto stopped=reporter.poll(1800,f);check(stopped&&stopped->frame.speed==0&&near(stopped->frame.z,-299.9225f));
 soe::PacketBuffer buf(stopped->payload.data(),stopped->payload.size());
 check(buf.readUint16()==5);check(buf.readUint32()==soe::MessageHash::compute("ObjControllerMessage"));check(buf.readUint32()==0x0b);check(buf.readUint32()==0x71);check(buf.readUint64()==42);check(buf.readUint32()==800);check(buf.readUint32()==2);
 check(buf.readFloat()==0);check(buf.readFloat()==0);check(buf.readFloat()==0);check(buf.readFloat()==1);check(buf.readFloat()==100);check(buf.readFloat()==2);check(near(buf.readFloat(),-299.9225f));check(buf.readFloat()==0);check(buf.remaining()==0);
 auto idle=reporter.poll(2200,f);check(idle&&idle->frame.speed==0&&idle->counter==3);
 check(reporter.poll(9000,f)->counter==4);check(!reporter.poll(9000,f));check(!reporter.poll(8999,f));
 for(uint64_t t=9400;t<=31000;t+=400)check(bool(reporter.poll(t,f)));
 check(!reporter.correctionIsCurrent(2));check(reporter.correctionIsCurrent(100));reporter.adoptCounter(100);check(reporter.poll(31400,f)->counter==101);reporter.adoptCounter(4);check(reporter.poll(31800,f)->counter==102);
 MovementFrame indoor{42,9,104,7,198,1.5707963267948966f,1.55f};MovementParentFrame building{100,2,200,1.5707963267948966f};
 auto local=movementWireFrame(indoor,building);check(local&&near(local->x,2)&&near(local->y,5)&&near(local->z,4)&&near(local->yaw,0));
 MovementReporter rooms(0);check(!rooms.poll(400,indoor));auto room=rooms.poll(400,indoor,building);check(room&&room->counter==1);
 soe::PacketBuffer rb(room->payload.data(),room->payload.size());rb.readUint16();rb.readUint32();rb.readUint32();check(rb.readUint32()==0xf1);check(rb.readUint64()==42);check(rb.readUint32()==400);check(rb.readUint32()==1);check(rb.readUint64()==9);
 check(rb.readFloat()==0);check(near(rb.readFloat(),0));check(rb.readFloat()==0);check(near(rb.readFloat(),1));check(near(rb.readFloat(),2));check(near(rb.readFloat(),5));check(near(rb.readFloat(),4));check(near(rb.readFloat(),1.55f));check(rb.remaining()==0);
 f.x=std::numeric_limits<float>::quiet_NaN();check(!rooms.poll(800,f));f.x=9000;check(!rooms.poll(800,f));f.x=100;f.speed=-1;check(!rooms.poll(800,f));f.speed=1;f.objectId=0;check(!rooms.poll(800,f));indoor.x=10000;check(!rooms.poll(800,indoor,building));
 MovementReporter wrap(0,UINT32_MAX-1);f={42,0,0,0,0,0,0};check(wrap.correctionIsCurrent(1));check(!wrap.correctionIsCurrent(UINT32_MAX-2));auto wrapped=wrap.poll(uint64_t(UINT32_MAX)+100,f);check(wrapped&&wrapped->timestamp==99);check(wrapped->counter==UINT32_MAX);check(wrap.poll(uint64_t(UINT32_MAX)+500,f)->counter==0);
 indoor={42,9,104,7,198,std::numeric_limits<float>::max(),1};building.yaw=-std::numeric_limits<float>::max();check(!movementWireFrame(indoor,building));
 std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"scope\":\"Movement stops, cadence, idle, wire fields, parent coordinates, correction counters and timestamp wrap\"}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
}
