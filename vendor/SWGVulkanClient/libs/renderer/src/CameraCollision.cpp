#include "renderer/CameraCollision.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace renderer {
namespace {
// Double intermediates avoid cancellation in the quadratic at zone coordinates.
struct V { double x, y, z; };
V v(assets::Float3 p) { return {p.x,p.y,p.z}; }
V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V operator*(V a,double s){return {a.x*s,a.y*s,a.z*s};}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool finite(assets::Float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float axis(assets::Float3 p,int i){return i==0?p.x:i==1?p.y:p.z;}
assets::Float3 low(assets::Float3 a,assets::Float3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
assets::Float3 high(assets::Float3 a,assets::Float3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
double segmentDistanceSquared(V p,V a,V b) {
    V e=b-a;double n=dot(e,e);
    V d=p-(a+e*(n>0?std::clamp(dot(p-a,e)/n,0.,1.):0.));return dot(d,d);
}
bool inTriangle(V p,V a,V b,V c,V n) {
    // Scale the tolerance with area; winding is intentionally double-sided.
    double tolerance=-1e-10*dot(n,n);
    return dot(cross(b-a,p-a),n)>=tolerance&&dot(cross(c-b,p-b),n)>=tolerance&&dot(cross(a-c,p-c),n)>=tolerance;
}
double triangleDistanceSquared(V p,V a,V b,V c) {
    V n=cross(b-a,c-a);double nn=dot(n,n);
    if(nn>1e-24) {
        double signedDistance=dot(p-a,n);V projection=p-n*(signedDistance/nn);
        if(inTriangle(projection,a,b,c,n))return signedDistance*signedDistance/nn;
    }
    return std::min({segmentDistanceSquared(p,a,b),segmentDistanceSquared(p,b,c),segmentDistanceSquared(p,c,a)});
}
template<class Accept> void roots(double a,double b,double c,Accept accept) {
    if(a<1e-24)return;
    double discriminant=b*b-4*a*c;
    // Tiny negative roundoff at a tangent is a contact, not a miss.
    double tolerance=1e-13*(b*b+std::abs(4*a*c)+1);
    if(discriminant < -tolerance)return;
    double s=std::sqrt(std::max(0.,discriminant));
    double q=-.5*(b+std::copysign(s,b));
    double t0=q/a,t1=std::abs(q)>1e-24?c/q:t0;
    if(t1<t0)std::swap(t0,t1);
    accept(t0);if(t1!=t0)accept(t1);
}
double sweepTriangle(V o,V d,double radius,V a,V b,V c,double limit,bool& overlap) {
    if(triangleDistanceSquared(o,a,b,c)<=radius*radius) {overlap=true;return 0;}
    double hit=limit;
    auto accept=[&](double t){if(t>=0&&t<=hit)hit=t;};
    V normal=cross(b-a,c-a);double nn=dot(normal,normal);
    if(nn>1e-24) {
        V n=normal*(1/std::sqrt(nn));double speed=dot(d,n),offset=dot(o-a,n);
        if(std::abs(speed)>1e-15)for(double sign:{-1.,1.}) {
            double t=(sign*radius-offset)/speed;
            if(t>=0&&t<=hit&&inTriangle(o+d*t-n*(sign*radius),a,b,c,normal))accept(t);
        }
    }
    for(auto edge: {std::pair{a,b},std::pair{b,c},std::pair{c,a}}) {
        V e=edge.second-edge.first,w=o-edge.first;double ee=dot(e,e);
        if(ee>1e-24) {
            V dp=d-e*(dot(d,e)/ee),wp=w-e*(dot(w,e)/ee);
            roots(dot(dp,dp),2*dot(dp,wp),dot(wp,wp)-radius*radius,[&](double t){
                double along=dot(w+d*t,e)/ee;
                if(along>=0&&along<=1)accept(t);
            });
        }
        roots(dot(d,d),2*dot(d,w),dot(w,w)-radius*radius,accept);
    }
    return hit;
}
bool intersectsBox(V o,V d,assets::Float3 lo,assets::Float3 hi,double radius,double limit) {
    double enter=0,leave=limit;
    for(int i=0;i<3;++i) {
        double p=i==0?o.x:i==1?o.y:o.z,delta=i==0?d.x:i==1?d.y:d.z;
        double l=axis(lo,i)-radius,h=axis(hi,i)+radius;
        if(std::abs(delta)<1e-15) {if(p<l||p>h)return false;}
        else {double a=(l-p)/delta,b=(h-p)/delta;if(a>b)std::swap(a,b);enter=std::max(enter,a);leave=std::min(leave,b);if(enter>leave)return false;}
    }
    return true;
}
} // namespace

CameraCollisionMesh::CameraCollisionMesh(std::span<const assets::Float3> positions,std::span<const uint32_t> indices) {
    if(indices.size()%3)throw std::invalid_argument("camera geometry: incomplete triangle");
    triangles_.reserve(indices.size()/3);
    for(size_t i=0;i<indices.size();i+=3) {
        for(int j=0;j<3;++j)if(indices[i+j]>=positions.size()||!finite(positions[indices[i+j]]))throw std::invalid_argument("camera geometry: invalid vertex");
        // Degenerate triangles remain valid segments/points for the sweep.
        triangles_.push_back({positions[indices[i]],positions[indices[i+1]],positions[indices[i+2]]});
    }
    if(!triangles_.empty())build(0,uint32_t(triangles_.size()));
}
uint32_t CameraCollisionMesh::build(uint32_t begin,uint32_t end) {
    uint32_t id=uint32_t(nodes_.size());nodes_.push_back({});
    assets::Float3 lo=triangles_[begin].a,hi=lo;
    for(uint32_t i=begin;i<end;++i)for(auto p:{triangles_[i].a,triangles_[i].b,triangles_[i].c}) {lo=low(lo,p);hi=high(hi,p);}
    nodes_[id]={lo,hi,begin,end-begin,0,0};
    if(end-begin>8) {
        auto extent=v(hi)-v(lo);int split=extent.y>extent.x?1:0;if(extent.z>(split==0?extent.x:extent.y))split=2;
        uint32_t mid=begin+(end-begin)/2;
        std::nth_element(triangles_.begin()+begin,triangles_.begin()+mid,triangles_.begin()+end,[&](const auto& a,const auto& b){
            return double(axis(a.a,split))+axis(a.b,split)+axis(a.c,split)<double(axis(b.a,split))+axis(b.b,split)+axis(b.c,split);
        });
        uint32_t left=build(begin,mid),right=build(mid,end);
        nodes_[id].left=left;nodes_[id].right=right;nodes_[id].count=0;
    }
    return id;
}
CameraCollisionMesh::Hit CameraCollisionMesh::sweep(assets::Float3 origin,assets::Float3 destination,float radius) const {
    if(!finite(origin)||!finite(destination)||!std::isfinite(radius)||radius<0)throw std::invalid_argument("camera sweep: invalid input");
    Hit result;if(nodes_.empty())return result;
    V o=v(origin),d=v(destination)-o;double nearest=std::nextafter(1.,2.);
    // Balanced BVH depth <= 32 for uint32 triangle counts. No per-frame allocation.
    uint32_t stack[64];size_t size=1;stack[0]=0;
    while(size) {
        const auto& node=nodes_[stack[--size]];
        if(!intersectsBox(o,d,node.low,node.high,radius,nearest))continue;
        if(!node.count){stack[size++]=node.left;stack[size++]=node.right;continue;}
        for(uint32_t i=node.begin;i<node.begin+node.count;++i) {
            ++result.triangleTests;const auto& tri=triangles_[i];bool overlap=false;
            double t=sweepTriangle(o,d,radius,v(tri.a),v(tri.b),v(tri.c),nearest,overlap);
            if(t<nearest){nearest=t;result.hit=true;}
            result.initialOverlap=result.initialOverlap||overlap;
        }
    }
    result.fraction=float(std::min(1.,nearest));return result;
}
float cameraClearanceRadius(float fov,float aspect,float nearPlane,float clearance) {
    if(!std::isfinite(fov)||fov<=0||fov>=3.14159265f||!std::isfinite(aspect)||aspect<=0||!std::isfinite(nearPlane)||nearPlane<=0||!std::isfinite(clearance)||clearance<0)throw std::invalid_argument("camera projection: invalid input");
    double t=std::tan(double(fov)*.5);
    return float(double(nearPlane)*std::sqrt(1+t*t*(1+double(aspect)*aspect))+clearance);
}
float CameraBoom::update(float safe,float dt) {
    if(!std::isfinite(safe)||safe<0||!std::isfinite(dt)||dt<0)throw std::invalid_argument("camera boom: invalid input");
    if(!initialized_||safe<distance_) {distance_=safe;initialized_=true;}
    else distance_=std::min(safe,distance_+(safe-distance_)*(-std::expm1(-8.f*dt)));
    return distance_;
}
} // namespace renderer
