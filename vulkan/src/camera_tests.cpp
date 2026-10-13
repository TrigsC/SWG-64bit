#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "renderer/CameraCollision.h"
#include <cmath>
#include <memory>
#include <unordered_map>
using assets::Float3;
using renderer::CameraCollisionMesh;
namespace {
CameraCollisionMesh wall(float x=2) {
    std::vector<Float3> p{{x,-10,-10},{x,10,-10},{x,10,10},{x,-10,10}};
    std::vector<uint32_t> i{0,1,2,0,2,3};return {p,i};
}
}
TEST_CASE("Double-sided wall, endpoint and long-step continuous clearance") {
    auto mesh=wall();
    CHECK(mesh.sweep({0,0,0},{10,0,0},.2f).fraction==doctest::Approx(.18));
    CHECK(mesh.sweep({4,0,0},{0,0,0},.2f).fraction==doctest::Approx(.45));
    CHECK(mesh.sweep({0,0,0},{1000,0,0},.2f).fraction==doctest::Approx(.0018));
    CHECK(mesh.sweep({0,0,0},{1.75f,0,0},.25f).hit);
    CHECK_FALSE(mesh.sweep({0,0,0},{1.7f,0,0},.2f).hit);
}
TEST_CASE("Near-plane radius covers viewport corners and increases with aspect") {
    float r=renderer::cameraClearanceRadius(.78539816f,2,.1f);
    float t=std::tan(.78539816f/2);
    CHECK(r>=std::sqrt(.01f+.01f*t*t*5));CHECK(r>.18f);
    CHECK(renderer::cameraClearanceRadius(.78539816f,4,.1f)>r);
    CHECK_THROWS(renderer::cameraClearanceRadius(0,2,.1f));
    CHECK_THROWS(renderer::cameraClearanceRadius(1,INFINITY,.1f));
}
TEST_CASE("Old center ray misses wall edge and corner that swept camera hits") {
    std::vector<Float3> p{{2,0,0},{2,4,0},{2,0,4}};std::vector<uint32_t> i{0,1,2};
    CameraCollisionMesh mesh(p,i);
    auto edge=mesh.sweep({0,-.1f,1},{4,-.1f,1},.2f);
    CHECK_FALSE(mesh.sweep({0,-.1f,1},{4,-.1f,1},0).hit);
    CHECK(edge.hit);CHECK(edge.fraction==doctest::Approx((2-std::sqrt(.04-.01))/4));
    auto corner=mesh.sweep({0,-.1f,-.1f},{4,-.1f,-.1f},.2f);
    CHECK(corner.hit);CHECK(corner.fraction==doctest::Approx((2-std::sqrt(.04-.02))/4));
    CHECK_FALSE(mesh.sweep({0,-.3f,-.3f},{4,-.3f,-.3f},.2f).hit);
}
TEST_CASE("Rendered roof and terrain protect camera without CMSH geometry") {
    std::vector<Float3> p{{-10,0,-10},{10,0,-10},{10,0,10},{-10,0,10}};
    std::vector<uint32_t> i{0,1,2,0,2,3};CameraCollisionMesh mesh(p,i);
    CHECK(mesh.sweep({0,2,0},{0,-2,0},.2f).fraction==doctest::Approx(.45));
    CHECK(mesh.sweep({0,-2,0},{0,2,0},.2f).fraction==doctest::Approx(.45));
    CameraCollisionMesh absent({},{});CHECK_FALSE(absent.sweep({0,2,0},{0,-2,0},.2f).hit);
}
TEST_CASE("Overlap, stationary, parallel, degenerate and malformed controls") {
    auto mesh=wall();auto hit=mesh.sweep({2,0,0},{4,0,0},.2f);
    CHECK(hit.initialOverlap);CHECK(hit.fraction==0);
    CHECK(mesh.sweep({2,0,0},{2,0,0},.2f).initialOverlap);
    CHECK_FALSE(mesh.sweep({0,0,0},{0,0,0},.2f).hit);
    CHECK_FALSE(mesh.sweep({0,0,0},{0,0,10},.2f).hit);
    std::vector<Float3> p{{2,0,0},{2,0,0},{2,0,0}};std::vector<uint32_t> i{0,1,2};
    CHECK(CameraCollisionMesh(p,i).sweep({0,0,0},{4,0,0},.2f).fraction==doctest::Approx(.45));
    CHECK_THROWS(mesh.sweep({NAN,0,0},{0,0,0},.2f));CHECK_THROWS(mesh.sweep({0,0,0},{0,0,0},-1));
    i[2]=5;CHECK_THROWS(CameraCollisionMesh(p,i));i={0,1};CHECK_THROWS(CameraCollisionMesh(p,i));
    p[0].x=INFINITY;i={0,1,2};CHECK_THROWS(CameraCollisionMesh(p,i));
}
TEST_CASE("Boom retracts immediately and releases consistently across frame rates") {
    renderer::CameraBoom camera;CHECK(camera.update(8,0)==8);CHECK(camera.update(1,.016f)==1);
    float released=0;for(int j=0;j<60;++j)released=camera.update(8,1.f/60);
    renderer::CameraBoom other;other.update(1,0);float second=0;for(int j=0;j<30;++j)second=other.update(8,1.f/30);
    CHECK(released==doctest::Approx(second).epsilon(.000001));CHECK(released<8);
    CHECK(camera.update(.5f,0)==.5f);camera.reset();CHECK(camera.update(8,0)==8);
    CHECK_THROWS(camera.update(-1,0));CHECK_THROWS(camera.update(1,NAN));
}
TEST_CASE("BVH agrees with unpartitioned sweeps and prunes distant triangles") {
    std::vector<Float3> p;std::vector<uint32_t> i;
    for(int j=0;j<1000;++j){auto base=uint32_t(p.size());float z=float(j)*10;p.insert(p.end(),{{2,-1,z-1},{2,1,z-1},{2,0,z+1}});i.insert(i.end(),{base,base+1,base+2});}
    CameraCollisionMesh mesh(p,i);
    for(int j=0;j<1000;j+=17) {
        Float3 a{0,0,float(j)*10},b{4,0,float(j)*10};auto hit=mesh.sweep(a,b,.2f);
        CHECK(hit.hit);CHECK(hit.fraction==doctest::Approx(.45));CHECK(hit.triangleTests<30);
    }
    // Query the same triangles individually as a partition-independent reference.
    for(int j=0;j<20;++j){Float3 a{0,.07f,float(j)*9.7f},b{4,.18f,float(j)*9.7f+.5f};float nearest=1;bool any=false;
        for(size_t k=0;k<i.size();k+=3){CameraCollisionMesh one(p,std::span<const uint32_t>(i).subspan(k,3));auto hit=one.sweep(a,b,.2f);nearest=std::min(nearest,hit.fraction);any=any||hit.hit;}
        auto actual=mesh.sweep(a,b,.2f);CHECK(actual.hit==any);CHECK(actual.fraction==doctest::Approx(nearest));
    }
}
