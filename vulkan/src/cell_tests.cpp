#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "worldmodel/CellTraversal.h"
#include "clientcommon/MovementReporting.h"
#include "soe/PacketBuffer.h"
#include <algorithm>
#include <cmath>
using namespace worldmodel;
namespace {
assets::PortalShape door(float z=0) {return {{{-1,0,z},{1,0,z},{1,2,z},{-1,2,z}}};}
const std::vector<assets::CellPortal> refs{{0,1}};
}
TEST_CASE("Interior parent resolves by building and cell number, never order or object ID arithmetic") {
    std::vector<ServerCellBinding> cells{{900,200,2},{501,201,1},{77,200,1},{600,200,-1}};
    CHECK(resolveCellParent(cells,200,0)==0);CHECK(resolveCellParent(cells,200,1)==77);
    CHECK(resolveCellParent(cells,200,2)==900);CHECK(resolveCellParent(cells,201,1)==501);
    CHECK_FALSE(resolveCellParent(cells,200,3));CHECK_FALSE(resolveCellParent(cells,0,0));
    cells.push_back({78,200,1});CHECK_FALSE(resolveCellParent(cells,200,1));
    cells.pop_back();cells.erase(cells.begin()+2);CHECK_FALSE(resolveCellParent(cells,200,1));
}
TEST_CASE("A proposed opening crossing does not commit through a blocked wall") {
    CellTraversal t;t.select(200,0);std::vector<assets::PortalShape> shapes{door()};
    auto p=t.propose(refs,shapes,{0,1,1},{0,1,-1},2);REQUIRE(p);
    CHECK(p->fraction==doctest::Approx(.5));CHECK(t.cell()==0);CHECK_FALSE(t.commit(*p,false));CHECK(t.cell()==0);
    REQUIRE(t.commit(*p,true));CHECK(t.cell()==1);CHECK_FALSE(t.commit(*p,true));
    const std::vector<assets::CellPortal> exits{{0,0}};auto out=t.propose(exits,shapes,{0,1,-1},{0,1,1},2);REQUIRE(out);REQUIRE(t.commit(*out,true));CHECK(t.cell()==0);
}
TEST_CASE("Exact polygon rejects the old sphere's false corner crossing") {
    CellTraversal t;t.select(200,0);std::vector<assets::PortalShape> shapes{door()};
    CHECK_FALSE(t.propose(refs,shapes,{1.1f,1,1},{1.1f,1,-1},2));
    CHECK_FALSE(t.propose(refs,shapes,{0,2.1f,1},{0,2.1f,-1},2));
    CHECK(t.propose(refs,shapes,{.999f,1,1},{.999f,1,-1},2));
    std::reverse(shapes[0].vertices.begin(),shapes[0].vertices.end());
    CHECK(t.propose(refs,shapes,{0,1,1},{0,1,-1},2));
}
TEST_CASE("Landing on a portal plane defers transition without losing the crossing") {
    CellTraversal t;t.select(200,0);std::vector<assets::PortalShape> shapes{door()};
    CHECK_FALSE(t.propose(refs,shapes,{0,1,1},{0,1,0},2));CHECK(t.cell()==0);
    CHECK_FALSE(t.propose(refs,shapes,{0,1,0},{0,1,0},2));
    auto p=t.propose(refs,shapes,{0,1,0},{0,1,-.1f},2);REQUIRE(p);CHECK(p->fraction==0);
    CHECK_FALSE(t.commit(*p,false));CHECK(t.cell()==0);
    CHECK_FALSE(t.propose(refs,shapes,{0,1,0},{0,1,.1f},2)); // back to source, not a crossing
    p=t.propose(refs,shapes,{0,1,.1f},{0,1,-.1f},2);REQUIRE(p);CHECK(t.commit(*p,true));
}
TEST_CASE("Relocation clears an old portal side even when returning to the same cell") {
    CellTraversal t;t.select(200,0);std::vector<assets::PortalShape> shapes{door()};
    CHECK_FALSE(t.propose(refs,shapes,{0,1,1},{0,1,0},2));
    t.select(0,0);t.select(200,0); // explicit authoritative relocation/reseed
    CHECK_FALSE(t.propose(refs,shapes,{0,1,0},{0,1,-.1f},2));
    CHECK(t.cell()==0);
    CHECK(t.propose(refs,shapes,{0,1,-.1f},{0,1,.1f},2));
}
TEST_CASE("Earliest opening wins; invalid, parallel and malformed controls cannot transition") {
    CellTraversal t;t.select(200,0);std::vector<assets::PortalShape> shapes{door(3),door(1)};
    std::vector<assets::CellPortal> two{{0,2},{1,1}};auto p=t.propose(two,shapes,{0,1,0},{0,1,4},3);REQUIRE(p);
    CHECK(p->destinationCell==1);CHECK(p->fraction==doctest::Approx(.25));
    CHECK_FALSE(t.propose(refs,shapes,{0,1,0},{1,1,0},3));
    CHECK_FALSE(t.propose(std::vector<assets::CellPortal>{{99,1}},shapes,{0,1,0},{0,1,4},3));
    CHECK_FALSE(t.propose(std::vector<assets::CellPortal>{{0,3}},shapes,{0,1,0},{0,1,4},3));
    shapes[0].vertices={{0,0,0},{0,0,0},{0,0,0}};CHECK_FALSE(t.propose(refs,shapes,{0,1,1},{0,1,-1},3));
    CHECK_THROWS(t.propose(refs,shapes,{NAN,1,1},{0,1,-1},3));
    CellCrossing wrong{201,0,1,0,.5f};CHECK_FALSE(t.commit(wrong,true));
}
TEST_CASE("Entering and leaving a rotated building selects correct wire parent and coordinates") {
    using namespace clientcommon;
    std::vector<ServerCellBinding> cells{{987654321,200,1}};
    MovementReporter r(0);MovementParentFrame b{-2736,5,2136,1.57079632679f};
    MovementFrame world{42,*resolveCellParent(cells,200,1),-2716,7,2132,1.57079632679f,1.55f};
    auto in=r.poll(400,world,b);REQUIRE(in);CHECK(in->frame.x==doctest::Approx(4));CHECK(in->frame.y==2);CHECK(in->frame.z==doctest::Approx(20));
    soe::PacketBuffer packet(in->payload.data(),in->payload.size());packet.readUint16();packet.readUint32();packet.readUint32();
    CHECK(packet.readUint32()==0xf1);CHECK(packet.readUint64()==42);CHECK(packet.readUint32()==400);CHECK(packet.readUint32()==1);CHECK(packet.readUint64()==987654321);
    world.parentId=*resolveCellParent(cells,200,0);auto out=r.poll(800,world);REQUIRE(out);CHECK(out->frame.x==world.x);CHECK(out->frame.y==world.y);CHECK(out->frame.z==world.z);
    soe::PacketBuffer outside(out->payload.data(),out->payload.size());outside.readUint16();outside.readUint32();outside.readUint32();CHECK(outside.readUint32()==0x71);
    world.parentId=987654321;CHECK_FALSE(r.poll(1200,world)); // unresolved frame cannot become outdoor
}

TEST_CASE("Indexed portal surfaces preserve bent openings and gaps") {
    CellTraversal t;t.select(200,0);
    assets::PortalShape mesh;
    // Two disconnected surfaces in perpendicular planes. Vertex storage
    // order is deliberately unsuitable as a boundary polygon.
    mesh.vertices={{-2,0,0},{-2,2,0},{-1,0,0},{-1,2,0},
                   {1,0,0},{1,2,0},{1,0,1},{1,2,1}};
    mesh.triangleIndices={0,2,1,1,2,3,4,6,5,5,6,7};
    std::vector<assets::PortalShape> shapes{mesh};
    auto a=t.propose(refs,shapes,{-1.5f,1,-1},{-1.5f,1,1},2);
    REQUIRE(a);CHECK(a->fraction==doctest::Approx(.5));CHECK(a->destinationCell==1);
    auto b=t.propose(refs,shapes,{0,1,.5f},{2,1,.5f},2);
    REQUIRE(b);CHECK(b->fraction==doctest::Approx(.5));
    CHECK_FALSE(t.propose(refs,shapes,{0,1,-1},{0,1,1},2)); // empty gap
    CHECK_FALSE(t.propose(refs,shapes,{-1.5f,3,-1},{-1.5f,3,1},2));
    std::reverse(shapes[0].triangleIndices.begin(),shapes[0].triangleIndices.end());
    CHECK(t.propose(refs,shapes,{-1.5f,1,-1},{-1.5f,1,1},2));
    shapes[0].triangleIndices.push_back(99);
    CHECK_FALSE(t.propose(refs,shapes,{-1.5f,1,-1},{-1.5f,1,1},2));
}
