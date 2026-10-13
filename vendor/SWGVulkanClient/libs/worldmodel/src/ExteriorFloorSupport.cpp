#include "worldmodel/ExteriorFloorSupport.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace worldmodel {
namespace {
bool finite(assets::Float3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
std::optional<float> heightAt(assets::Float3 a,assets::Float3 b,assets::Float3 c,assets::Float3 p,float slack) {
    double determinant=(double(b.z)-c.z)*(double(a.x)-c.x)+(double(c.x)-b.x)*(double(a.z)-c.z);
    if(std::abs(determinant)<1e-10)return {};
    double u=((double(b.z)-c.z)*(double(p.x)-c.x)+(double(c.x)-b.x)*(double(p.z)-c.z))/determinant;
    double v=((double(c.z)-a.z)*(double(p.x)-c.x)+(double(a.x)-c.x)*(double(p.z)-c.z))/determinant;
    double w=1-u-v;
    // Each weight's signed distance from its opposite edge scales by the
    // triangle area/edge length. Large triangles get no larger slack.
    auto length=[](assets::Float3 x,assets::Float3 y){return std::hypot(double(x.x)-y.x,double(x.z)-y.z);};
    const double area=std::abs(determinant);
    if(u*area < -double(slack)*length(b,c)||v*area < -double(slack)*length(c,a)||w*area < -double(slack)*length(a,b))return {};
    return float(u*a.y+v*b.y+w*c.y);
}
}
ExteriorFloorSupport queryExteriorFloorSupport(const assets::FloorCollisionMesh& floor,const assets::MeshData& collision,
    assets::Float3 p,std::optional<float> terrain,ExteriorFloorLimits limits) {
    if(!finite(p)||(terrain&&!std::isfinite(*terrain))||!std::isfinite(limits.stepUp)||limits.stepUp<0||
        !std::isfinite(limits.dropWithoutTerrain)||limits.dropWithoutTerrain<0||!std::isfinite(limits.edgeSlack)||limits.edgeSlack<0)
        throw std::invalid_argument("Exterior floor query: invalid input");
    ExteriorFloorSupport result;result.height=terrain.value_or(p.y);
    result.source=terrain?ExteriorFloorSupport::Source::Terrain:ExteriorFloorSupport::Source::Hold;
    const float reference=std::max(p.y,terrain.value_or(p.y));
    auto consider=[&](const auto& positions,const auto& indices,ExteriorFloorSupport::Source source) {
        if(indices.size()%3)throw std::invalid_argument("Exterior floor query: incomplete triangle");
        float bestDelta=std::numeric_limits<float>::infinity();
        bool accepted=false;
        for(size_t i=0;i<indices.size();i+=3) {
            for(int k=0;k<3;++k)if(indices[i+k]>=positions.size()||!finite(positions[indices[i+k]]))
                throw std::invalid_argument("Exterior floor query: invalid mesh vertex");
            auto height=heightAt(positions[indices[i]],positions[indices[i+1]],positions[indices[i+2]],p,limits.edgeSlack);
            if(!height||!std::isfinite(*height))continue;
            ++result.projectedCandidates;
            if(terrain&&*height<*terrain-.002f){++result.buriedCandidates;continue;}
            if(*height>reference+limits.stepUp||(!terrain&&*height<p.y-limits.dropWithoutTerrain)) {
                ++result.unreachableCandidates;continue;
            }
            const float supported=std::max(*height,terrain.value_or(*height));
            const float delta=std::abs(supported-reference);
            if(delta<bestDelta||(delta==bestDelta&&supported>result.height)) {
                bestDelta=delta;result.height=supported;result.triangle=i/3;
                result.source=source;accepted=true;
            }
        }
        return accepted;
    };
    // Authored FLR support has priority. Its gaps may have real CMSH support
    // (e.g. a roof without navigation triangles). That fallback uses the same
    // direct coverage and reachability checks, never a ray from 50m above feet.
    if(!consider(floor.positions,floor.triangleVertexIndices,ExteriorFloorSupport::Source::FloorMesh))
        consider(collision.positions,collision.indices,ExteriorFloorSupport::Source::CollisionMesh);
    return result;
}
const char* exteriorFloorSourceName(ExteriorFloorSupport::Source source) {
    switch(source) {
        case ExteriorFloorSupport::Source::Hold:return "hold";
        case ExteriorFloorSupport::Source::Terrain:return "terrain";
        case ExteriorFloorSupport::Source::FloorMesh:return "floor_mesh";
        case ExteriorFloorSupport::Source::CollisionMesh:return "collision_mesh";
    }return "unknown";
}
} // namespace worldmodel
