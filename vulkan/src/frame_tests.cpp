#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "worldmodel/SpatialTransform.h"
#include "clientcommon/MovementReporting.h"
#include "soe/PacketBuffer.h"
#include <DirectXMath.h>
#include <limits>
using namespace worldmodel;
namespace {
float distance(assets::Float3 a,assets::Float3 b){return std::hypot(std::hypot(a.x-b.x,a.z-b.z),a.y-b.y);}
WorldObject object(uint64_t id,assets::Float3 p,Quaternion q) {
    WorldObject o;o.objectId=id;o.x=p.x;o.y=p.y;o.z=p.z;o.transformMessagesSeen=1;o.fullRotationCurrent=true;
    o.quatX=q.x;o.quatY=q.y;o.quatZ=q.z;o.quatW=q.w;return o;
}
// Rodrigues axis-angle oracle, independent of the production quaternion cross products.
assets::Float3 oracle(Quaternion q,assets::Float3 v) {
    double n=std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    double x=q.x/n,y=q.y/n,z=q.z/n,w=q.w/n,s=std::sqrt(x*x+y*y+z*z);
    if(s<1e-12)return v;x/=s;y/=s;z/=s;double c=2*w*w-1,t=1-c,a=2*w*s;
    return {float((c+x*x*t)*v.x+(x*y*t-z*a)*v.y+(x*z*t+y*a)*v.z),
        float((y*x*t+z*a)*v.x+(c+y*y*t)*v.y+(y*z*t-x*a)*v.z),
        float((z*x*t-y*a)*v.x+(z*y*t+x*a)*v.y+(c+z*z*t)*v.z)};
}
}
TEST_CASE("Precise rotations agree with axis-angle and ARM64 renderer math") {
    for(Quaternion raw:std::vector<Quaternion>{{0,0,0,1},{0,.383,0,.923},{.12f,.23f,-.31f,.89f},{-.5f,.5f,.5f,.5f},{0,7,0,3}}) {
        auto q=normalizedRotation(raw);REQUIRE(q);
        for(assets::Float3 p:std::vector<assets::Float3>{{20,2,17},{-41,9,-30},{0,1,0},{0,0,1}}) {
            CHECK(distance(rotateVector(*q,p),oracle(raw,p))<.00002f);
            auto matrix=DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(q->x,q->y,q->z,q->w));
            DirectX::XMFLOAT3 rendered;DirectX::XMStoreFloat3(&rendered,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(p.x,p.y,p.z,1),matrix));
            CHECK(distance({rendered.x,rendered.y,rendered.z},oracle(raw,p))<.00002f);
            auto inverse=inverseRotation(*q);CHECK(distance(rotateVector(inverse,rotateVector(*q,p)),p)<.00003f);
        }
    }
}
TEST_CASE("Translated exact frames preserve doorway coordinates and retain the lossy failure") {
    for(float yaw:{.001f,.7312345f,1.232345f,-2.12431f,3.14159265f})for(assets::Float3 p:std::vector<assets::Float3>{{0,2.7f,17.3692f},{14,9,-32},{-40,-12,25}}) {
        SpatialTransform frame{{-4807.123f,41.789f,2743.213f},headingRotation(yaw)};
        auto expected=oracle(frame.rotation,p);expected.x+=frame.origin.x;expected.y+=frame.origin.y;expected.z+=frame.origin.z;
        CHECK(distance(frame.toWorld(p),expected)<.0006f);CHECK(distance(frame.toLocal(expected),p)<.0006f);
    }
    auto q=headingRotation(.7312345f);auto exact=object(200,{-4807,42,2743},q);exact.direction=11;
    auto right=objectLocalTransform(exact)->toWorld({0,2.7f,17.3692f});exact.fullRotationCurrent=false;
    auto legacy=objectLocalTransform(exact)->toWorld({0,2.7f,17.3692f});CHECK(distance(right,legacy)>.6f);
}
TEST_CASE("Latest orientation source and malformed frames cannot use stale quaternions") {
    auto o=object(1,{0,0,0},headingRotation(.7312345f));o.direction=25;
    CHECK(rotationHeading(*objectRotation(o))==doctest::Approx(.7312345f));
    o.fullRotationCurrent=false;CHECK(rotationHeading(*objectRotation(o))==doctest::Approx(1.57079632679f));
    o.fullRotationCurrent=true;o.quatX=o.quatY=o.quatZ=o.quatW=0;CHECK_FALSE(objectLocalTransform(o));
    o.quatW=1;o.x=NAN;CHECK_FALSE(objectLocalTransform(o));o.x=0;o.transformMessagesSeen=0;CHECK_FALSE(objectLocalTransform(o));
    CHECK_FALSE(normalizedRotation({0,INFINITY,0,1}));
    CHECK(SpatialTransform{{0,0,0},headingRotation(.7312345f)}.preservesWorldUp());
    CHECK_FALSE(SpatialTransform{{0,0,0},{.1f,0,0,.9949874f}}.preservesWorldUp());
}
TEST_CASE("Contained position and orientation compose, explicit outdoor zero wins over stale containment") {
    auto b=object(200,{-4800,41,2700},headingRotation(.7312345f));
    WorldObject cell;cell.objectId=501;cell.typeTag=ObjectTypeTag::Cell;cell.containerId=200;cell.containmentMessagesSeen=1;
    auto child=object(42,{3,2,17},headingRotation(.12345f));child.parentId=501;
    std::unordered_map<uint64_t,WorldObject> scene{{200,b},{501,cell}};
    auto pose=resolveWorldTransform(scene,child);REQUIRE(pose);
    auto expected=oracle(headingRotation(.7312345f),{3,2,17});expected.x+=b.x;expected.y+=b.y;expected.z+=b.z;
    CHECK(distance(pose->origin,expected)<.001f);CHECK(rotationHeading(pose->rotation)==doctest::Approx(.8546845f));
    child.parentId=0;child.containerId=501;CHECK(resolveWorldTransform(scene,child)); // stationary scene seed uses logical cell
    child.spatialParentKnown=true;pose=resolveWorldTransform(scene,child);REQUIRE(pose);CHECK(pose->origin.x==3);CHECK(pose->origin.z==17);
    child.parentId=999;CHECK_FALSE(resolveWorldTransform(scene,child));child.parentId=501;scene.erase(200);CHECK_FALSE(resolveWorldTransform(scene,child));
    scene[200]=b;scene[501].typeTag=ObjectTypeTag::Tangible;CHECK_FALSE(resolveWorldTransform(scene,child));
}
TEST_CASE("Full rotated parent serializes exact local quaternion and coordinates") {
    using namespace clientcommon;
    auto parentQ=*normalizedRotation({.1f,.2f,-.13f,.91f});auto localQ=headingRotation(.23f);
    SpatialTransform parent{{-4800,42,2700},parentQ};auto p=parent.toWorld({3,2,17});
    auto worldQ=multiplyRotation(parentQ,localQ);
    MovementFrame frame{42,98765,p.x,p.y,p.z,rotationHeading(worldQ),1.55f,worldQ};
    MovementParentFrame basis{-4800,42,2700,0,parentQ};MovementReporter reporter(0);
    auto report=reporter.poll(400,frame,basis);REQUIRE(report);CHECK(distance({report->frame.x,report->frame.y,report->frame.z},{3,2,17})<.001f);
    soe::PacketBuffer packet(report->payload.data(),report->payload.size());packet.readUint16();packet.readUint32();packet.readUint32();CHECK(packet.readUint32()==0xf1);CHECK(packet.readUint64()==42);CHECK(packet.readUint32()==400);CHECK(packet.readUint32()==1);CHECK(packet.readUint64()==98765);
    Quaternion wire{packet.readFloat(),packet.readFloat(),packet.readFloat(),packet.readFloat()};
    CHECK(distance(rotateVector(wire,{0,0,1}),rotateVector(localQ,{0,0,1}))<.00001f);
    CHECK(std::abs(packet.readFloat()-3)<.001);CHECK(std::abs(packet.readFloat()-2)<.001);CHECK(std::abs(packet.readFloat()-17)<.001);
    CHECK(packet.readFloat()==doctest::Approx(1.55f));CHECK(packet.remaining()==0);
    frame.parentId=0;report=reporter.poll(800,frame);REQUIRE(report);
    wire=*report->frame.orientation;CHECK(std::abs(wire.x)>.01f);CHECK(std::abs(wire.z)>.01f);CHECK(distance(rotateVector(wire,{0,0,1}),rotateVector(worldQ,{0,0,1}))<.00001f);
    basis.rotation=Quaternion{0,0,0,0};frame.parentId=98765;CHECK_FALSE(reporter.poll(1200,frame,basis));
}
