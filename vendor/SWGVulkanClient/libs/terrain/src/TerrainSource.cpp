#include "terrain/TerrainSource.h"
#include <cmath>
#include <stdexcept>
namespace terrain {
float TerrainSource::querySurfaceHeight(float x,float z) const {
    float step=chunkWidthInMeters()/tilesPerChunk();
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(step)||step<=0)throw std::runtime_error("Invalid terrain surface query");
    float x0=std::floor(x/step)*step,z0=std::floor(z/step)*step;
    float u=(x-x0)/step,v=(z-z0)/step;
    float h10=queryHeight(x0+step,z0),h01=queryHeight(x0,z0+step);
    if(u+v<=1) {float h00=queryHeight(x0,z0);return h00+u*(h10-h00)+v*(h01-h00);}
    float h11=queryHeight(x0+step,z0+step);
    return h11+(1-u)*(h01-h11)+(1-v)*(h10-h11);
}
}
