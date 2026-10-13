#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "worldmodel/ExteriorFloorSupport.h"
#include <cmath>
using assets::Float3;
using worldmodel::ExteriorFloorSupport;
using worldmodel::queryExteriorFloorSupport;
namespace {
assets::FloorCollisionMesh plane(float y,float extent=10) {
    assets::FloorCollisionMesh f;f.positions={{-extent,y,-extent},{extent,y,-extent},{extent,y,extent},{-extent,y,extent}};
    f.triangleVertexIndices={0,1,2,0,2,3};return f;
}
void add(assets::FloorCollisionMesh& a,const assets::FloorCollisionMesh& b) {
    uint32_t base=a.positions.size();a.positions.insert(a.positions.end(),b.positions.begin(),b.positions.end());
    for(auto i:b.triangleVertexIndices)a.triangleVertexIndices.push_back(base+i);
}
}
TEST_CASE("Buried exterior floor cannot pull stationary feet under terrain") {
    auto f=plane(-15.5f);
    for(float ground:{0.f,5.f,-2.f}) {
        auto hit=queryExteriorFloorSupport(f,{}, {0,ground,0},ground);
        CHECK(hit.source==ExteriorFloorSupport::Source::Terrain);CHECK(hit.height==ground);
        CHECK(hit.buriedCandidates==2);CHECK_FALSE(hit.triangle.has_value());
    }
}
TEST_CASE("Overlapping levels select reachable support rather than a roof") {
    auto f=plane(-8.5f);add(f,plane(.5f));add(f,plane(9.5f));
    auto ground=queryExteriorFloorSupport(f,{}, {0,0,0},0);
    CHECK(ground.height==.5f);CHECK(ground.buriedCandidates==2);CHECK(ground.unreachableCandidates==2);
    auto roof=queryExteriorFloorSupport(f,{}, {0,9.5f,0},0);
    CHECK(roof.height==9.5f);CHECK(roof.source==ExteriorFloorSupport::Source::FloorMesh);
    auto below=queryExteriorFloorSupport(f,{}, {0,-8.5f,0},std::nullopt);
    CHECK(below.height==-8.5f); // No terrain policy is imposed when absent.
}
TEST_CASE("No extrapolation beyond mesh; physical edge tolerance independent of size") {
    for(float size:{1.f,100.f,1000.f}) {
        auto f=plane(.5f,size);
        CHECK(queryExteriorFloorSupport(f,{}, {size+.0005f,0,0},0).height==doctest::Approx(.5f));
        auto outside=queryExteriorFloorSupport(f,{}, {size+.002f,0,0},0);
        CHECK(outside.height==0);CHECK_FALSE(outside.triangle.has_value());
        CHECK(queryExteriorFloorSupport(f,{}, {size+5,0,0},0).height==0);
    }
}
TEST_CASE("Continuous slope follows authored height in both directions") {
    auto f=plane(0);for(auto& p:f.positions)p.y=(p.z+10)*.2f;
    float y=0;
    for(int direction:{1,-1})for(int i=0;i<=200;++i) {
        float z=direction==1?-10+i*.1f:10-i*.1f;
        auto hit=queryExteriorFloorSupport(f,{}, {0,y,z},0);
        CHECK(hit.source==ExteriorFloorSupport::Source::FloorMesh);
        CHECK(hit.height==doctest::Approx((z+10)*.2f));y=hit.height;
    }
}
TEST_CASE("FLR takes priority; CMSH gap support must also be directly covered and reachable") {
    assets::MeshData collision;collision.positions=plane(.5f).positions;collision.indices=plane(.5f).triangleVertexIndices;
    auto narrow=plane(.5f,1);
    CHECK(queryExteriorFloorSupport(narrow,collision,{2,0,0},0).source==ExteriorFloorSupport::Source::CollisionMesh);
    collision.positions=plane(9.5f).positions;
    CHECK(queryExteriorFloorSupport(narrow,collision,{2,0,0},0).source==ExteriorFloorSupport::Source::Terrain);
    auto roof=queryExteriorFloorSupport(narrow,collision,{2,9.5f,0},0);
    CHECK(roof.source==ExteriorFloorSupport::Source::CollisionMesh);CHECK(roof.height==9.5f);
    CHECK(queryExteriorFloorSupport(narrow,collision,{0,0,0},0).source==ExteriorFloorSupport::Source::FloorMesh);
    collision.positions=plane(.5f).positions;
    CHECK(queryExteriorFloorSupport({},collision,{2,0,0},0).source==ExteriorFloorSupport::Source::CollisionMesh);
    auto noData=queryExteriorFloorSupport({}, {},{0,8,0},std::nullopt);
    CHECK(noData.height==8);CHECK(noData.source==ExteriorFloorSupport::Source::Hold);
    CHECK(queryExteriorFloorSupport(plane(8),{}, {0,0,0},0).height==0);
}
TEST_CASE("Vertical and malformed input cannot produce fabricated floor heights") {
    auto f=plane(0);f.positions={{0,0,0},{0,10,0},{0,0,10},{0,10,10}};
    CHECK(queryExteriorFloorSupport(f,{}, {0,0,0},0).source==ExteriorFloorSupport::Source::Terrain);
    f=plane(0);f.triangleVertexIndices[0]=50;CHECK_THROWS(queryExteriorFloorSupport(f,{}, {0,0,0},0));
    f=plane(0);f.triangleVertexIndices.pop_back();CHECK_THROWS(queryExteriorFloorSupport(f,{}, {0,0,0},0));
    f=plane(0);f.positions[0].x=NAN;CHECK_THROWS(queryExteriorFloorSupport(f,{}, {0,0,0},0));
    CHECK_THROWS(queryExteriorFloorSupport({}, {}, {NAN,0,0},0));
    CHECK_THROWS(queryExteriorFloorSupport({}, {}, {0,0,0},INFINITY));
    CHECK_THROWS(queryExteriorFloorSupport({}, {}, {0,0,0},0,{-1,1,.001f}));
}
