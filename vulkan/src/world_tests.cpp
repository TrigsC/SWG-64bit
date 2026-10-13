#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ClientArchiveOrder.h"
#include "terrain/TerrainSource.h"
#include "terrain/TerrainMesh.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
struct CurvedTerrain : terrain::TerrainSource {
    float queryHeight(float x,float z) const override {return .07f*x*x+.11f*z*z+.3f*x*z;}
    float chunkWidthInMeters() const override {return 32;}
    uint32_t tilesPerChunk() const override {return 4;}
    terrain::ChunkHeightData generateChunk(terrain::ChunkCoord coord) const override {
        terrain::ChunkHeightData c;c.coord=coord;c.verticesPerSide=5;c.tileWidthInMeters=8;
        c.worldOriginX=coord.chunkX*32;c.worldOriginZ=coord.chunkZ*32;
        for(int z=0;z<5;++z)for(int x=0;x<5;++x){c.heights.push_back(queryHeight(c.worldOriginX+x*8,c.worldOriginZ+z*8));c.colors.push_back({100,100,100});}
        return c;
    }
};
// Intersect the vertical ray with actual generated triangles, independent
// of TerrainSource's grid interpolation implementation.
float meshHeight(const terrain::TerrainMeshData& mesh,float x,float z) {
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        float det=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
        if(std::abs(det)<1e-6f)continue;
        float u=((b.z-c.z)*(x-c.x)+(c.x-b.x)*(z-c.z))/det;
        float v=((c.z-a.z)*(x-c.x)+(a.x-c.x)*(z-c.z))/det;
        if(u>=-1e-5f&&v>=-1e-5f&&u+v<=1.00001f)return u*a.y+v*b.y+(1-u-v)*c.y;
    }
    throw std::runtime_error("No triangle covers test point");
}
struct TempDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("swg-world-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory(){std::filesystem::create_directory(path);}
    ~TempDirectory(){std::filesystem::remove_all(path);}
};
}
TEST_CASE("Ground collision matches rendered triangles in positive and negative chunks") {
    CurvedTerrain source;
    for(int cx:{-2,-1,0,1})for(int cz:{-1,0,1}) {
        auto chunk=source.generateChunk({cx,cz});auto mesh=terrain::buildChunkMesh(chunk);
        for(float x:{0.f,1.5f,7.5f,8.f,19.f,31.5f})for(float z:{0.f,2.f,7.f,8.f,23.f,31.5f}) {
            float wx=chunk.worldOriginX+x,wz=chunk.worldOriginZ+z;
            CHECK(source.querySurfaceHeight(wx,wz)==doctest::Approx(meshHeight(mesh,wx,wz)).epsilon(.00002));
        }
    }
    CHECK(std::abs(source.queryHeight(3,2)-source.querySurfaceHeight(3,2))>.1f);
    CHECK_THROWS(source.querySurfaceHeight(INFINITY,0));
}
TEST_CASE("Configured patches take precedence and archive names are deduplicated") {
    TempDirectory dir;
    std::ofstream(dir.path/"base.tre").put('x');std::ofstream(dir.path/"patch.tre").put('x');
    std::ofstream(dir.path/"swgemu_live.cfg")<<"searchTree_00_00=base.tre\nsearchTree_00_07=patch.tre\nsearchTree_00_02=patch.tre\n";
    auto order=dummyclient::clientArchiveOrder(dir.path.string(),{"ignored.tre"});
    REQUIRE(order.size()==2);CHECK(order[0]=="patch.tre");CHECK(order[1]=="base.tre");
}
TEST_CASE("Configured missing archives fail explicitly and legacy fallback is retained") {
    TempDirectory dir;
    CHECK(dummyclient::clientArchiveOrder(dir.path.string(),{"legacy.tre"})==std::vector<std::string>{"legacy.tre"});
    std::ofstream(dir.path/"swgemu_live.cfg")<<"searchTree_00_01=missing.tre\n";
    CHECK_THROWS(dummyclient::clientArchiveOrder(dir.path.string(),{}));
}
