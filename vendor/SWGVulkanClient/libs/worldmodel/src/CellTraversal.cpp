#include "worldmodel/CellTraversal.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace worldmodel {
namespace {
bool finite(assets::Float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct Plane {double x,y,z;assets::Float3 origin;};
std::optional<Plane> plane(const assets::PortalShape& shape) {
    if(shape.vertices.size()<3)return {};
    Plane n{0,0,0,shape.vertices.front()};
    for(size_t i=0;i<shape.vertices.size();++i) {
        auto a=shape.vertices[i],b=shape.vertices[(i+1)%shape.vertices.size()];
        if(!finite(a))return {};
        n.x+=(double(a.y)-b.y)*(double(a.z)+b.z);
        n.y+=(double(a.z)-b.z)*(double(a.x)+b.x);
        n.z+=(double(a.x)-b.x)*(double(a.y)+b.y);
    }
    double length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    if(length<1e-9)return {};
    n.x/=length;n.y/=length;n.z/=length;
    for(auto v:shape.vertices)if(std::abs(n.x*(v.x-n.origin.x)+n.y*(v.y-n.origin.y)+n.z*(v.z-n.origin.z))>.002)return {};
    return n;
}
double side(const Plane& n,assets::Float3 p) {
    return n.x*(double(p.x)-n.origin.x)+n.y*(double(p.y)-n.origin.y)+n.z*(double(p.z)-n.origin.z);
}
bool contains(const assets::PortalShape& shape,const Plane& n,assets::Float3 p) {
    int drop=std::abs(n.x)>std::abs(n.y)?0:1;
    if(std::abs(n.z)>(drop==0?std::abs(n.x):std::abs(n.y)))drop=2;
    auto project=[&](assets::Float3 v){return drop==0?std::pair<double,double>{v.y,v.z}:drop==1?std::pair<double,double>{v.x,v.z}:std::pair<double,double>{v.x,v.y};};
    auto [x,y]=project(p);bool inside=false;
    for(size_t i=0,j=shape.vertices.size()-1;i<shape.vertices.size();j=i++) {
        auto [ax,ay]=project(shape.vertices[j]);auto [bx,by]=project(shape.vertices[i]);
        double dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
        double t=length?std::clamp(((x-ax)*dx+(y-ay)*dy)/length,0.,1.):0.;
        if(std::hypot(x-ax-t*dx,y-ay-t*dy)<=.001)return true;
        if((ay>y)!=(by>y) && x<ax+(y-ay)*dx/(by-ay))inside=!inside;
    }
    return inside;
}
}
std::optional<uint64_t> resolveCellParent(std::span<const ServerCellBinding> cells,uint64_t building,size_t cell) {
    if(!building)return {};
    if(cell==0)return uint64_t(0);
    std::optional<uint64_t> result;
    for(auto c:cells)if(c.buildingId==building&&c.number>0&&size_t(c.number)==cell&&c.objectId) {
        if(result&&*result!=c.objectId)return {};
        result=c.objectId;
    }
    return result;
}
void CellTraversal::select(uint64_t building,size_t cell) {
    if(building_!=building||cell_!=cell){lastSides_.clear();building_=building;cell_=cell;}
}
std::optional<CellCrossing> CellTraversal::propose(std::span<const assets::CellPortal> portals,
    std::span<const assets::PortalShape> shapes,assets::Float3 from,assets::Float3 to,size_t cellCount) {
    if(!finite(from)||!finite(to))throw std::invalid_argument("Cell traversal: nonfinite segment");
    std::optional<CellCrossing> best;
    for(auto ref:portals) {
        if(ref.portalShapeIndex>=shapes.size()||ref.adjacentCellIndex>=cellCount||ref.adjacentCellIndex==cell_)continue;
        const auto& shape=shapes[ref.portalShapeIndex];
        auto testOpening=[&](const assets::PortalShape& opening,uint32_t part) {
            auto n=plane(opening);if(!n)return;
            // Each indexed triangle has its own plane; a bent portal cannot
            // share remembered side state across its different surfaces.
            uint64_t key=(uint64_t(ref.portalShapeIndex)<<32)|part;
            double actualFrom=side(*n,from),end=side(*n,to),start=actualFrom;
            if(std::abs(start)>1e-5)lastSides_[key]=float(start);
            else {auto old=lastSides_.find(key);if(old==lastSides_.end())return;start=old->second;}
            if(std::abs(end)<=1e-5||start*end>=0)return;
            float t=std::abs(actualFrom)<=1e-5?0.f:float(actualFrom/(actualFrom-end));
            assets::Float3 hit{from.x+t*(to.x-from.x),from.y+t*(to.y-from.y),from.z+t*(to.z-from.z)};
            if(contains(opening,*n,hit)&&(!best||t<best->fraction))best=CellCrossing{building_,cell_,ref.adjacentCellIndex,ref.portalShapeIndex,t};
        };
        if(shape.triangleIndices.empty())testOpening(shape,0);
        else {
            if(shape.triangleIndices.size()%3 || std::any_of(shape.triangleIndices.begin(),shape.triangleIndices.end(),
                [&](auto i){return i>=shape.vertices.size();}))continue;
            for(size_t i=0;i<shape.triangleIndices.size();i+=3) {
                assets::PortalShape triangle;
                for(size_t j=0;j<3;++j)triangle.vertices.push_back(shape.vertices[shape.triangleIndices[i+j]]);
                testOpening(triangle,static_cast<uint32_t>(i/3));
            }
        }
    }
    return best;
}
bool CellTraversal::commit(const CellCrossing& cross,bool accepted) {
    if(!accepted||cross.buildingId!=building_||cross.sourceCell!=cell_)return false;
    select(building_,cross.destinationCell);return true;
}
} // namespace worldmodel
