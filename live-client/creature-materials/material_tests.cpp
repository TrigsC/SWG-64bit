#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "MaterialResolver.h"
#include "ClientArchiveOrder.h"
#include "assets/TreArchive.h"
#include "assets/ShaderTemplate.h"
#include <filesystem>
#include <memory>
#include <map>
#include <algorithm>

namespace {
struct Archives {
    std::vector<std::unique_ptr<assets::TreArchive>> archives;
    Archives() {
        for (auto name : dummyclient::clientArchiveOrder(SWG_CLIENT_ROOT, {}))
            archives.push_back(std::make_unique<assets::TreArchive>((std::filesystem::path(SWG_CLIENT_ROOT)/name).string()));
        if (archives.empty()) throw std::runtime_error("Asset fixture requires configured TRE archives");
    }
    std::optional<std::vector<uint8_t>> extract(const std::string& path) const {
        for (const auto& archive : archives) if (archive->contains(path)) return archive->extract(path);
        return {};
    }
};
const Archives& fixtures() {static Archives archives;return archives;}
bool equal(assets::Float3 a, assets::Float3 b) {return a.x==b.x&&a.y==b.y&&a.z==b.z;}
bool equal(assets::Float2 a, assets::Float2 b) {return a.x==b.x&&a.y==b.y;}
assets::SkeletalMeshSubmesh triangle(const std::string& shader) {
    assets::SkeletalMeshSubmesh result;result.shaderFilename=shader;
    result.positions={{1,2,3},{-4,5,6},{7,8,-9}};
    result.normals={{0,1,0},{0,1,0},{0,1,0}};
    result.uv0={{.125f,.25f},{.5f,.75f},{1,.625f}};result.indices={2,0,1};return result;
}
void checkGeometry(const assets::MeshData& actual, const assets::SkeletalMeshSubmesh& expected) {
    REQUIRE(actual.positions.size()==expected.positions.size());REQUIRE(actual.normals.size()==expected.normals.size());
    REQUIRE(actual.uv0.size()==expected.uv0.size());
    CHECK(std::equal(actual.positions.begin(),actual.positions.end(),expected.positions.begin(),[](auto a,auto b){return equal(a,b);}));
    CHECK(std::equal(actual.normals.begin(),actual.normals.end(),expected.normals.begin(),[](auto a,auto b){return equal(a,b);}));
    CHECK(std::equal(actual.uv0.begin(),actual.uv0.end(),expected.uv0.begin(),[](auto a,auto b){return equal(a,b);}));
    CHECK(actual.indices==expected.indices);
}
}
TEST_CASE("Missing shader, missing DDS and malformed data preserve submesh geometry") {
    for (const std::string shader : {"", "shader/not-found.sht", "shader/hum_m_body.sht"}) {
        auto input=triangle(shader);
        auto result=dummyclient::resolveSkeletalSubmesh(input, [](const std::string&) -> std::optional<std::vector<uint8_t>> {return {};});
        checkGeometry(result.mesh,input);CHECK(result.shaderFilename==shader);CHECK_FALSE(result.texture);CHECK(result.textureFilename.empty());
    }
    for (int failure : {0,1,2,3,4}) {
        auto input=triangle("shader/hum_m_body.sht");
        auto result=dummyclient::resolveSkeletalSubmesh(input, [&](const std::string& path)->std::optional<std::vector<uint8_t>> {
            if (failure==3 || (failure==4&&path.starts_with("texture/")))throw std::runtime_error("Simulated archive read failure");
            if (failure==0 || (failure==2 && path.starts_with("texture/")))return std::vector<uint8_t>{0,1,2,3};
            if (path.starts_with("shader/"))return fixtures().extract(path);
            return {};
        });
        checkGeometry(result.mesh,input);CHECK_FALSE(result.texture);CHECK(result.textureFilename.empty());
    }
}
TEST_CASE("Each material retains its own UVs, indices and MAIN texture") {
    const std::vector<std::string> paths={
        "object/creature/player/shared_human_male.iff", "object/creature/player/shared_human_female.iff",
        "object/mobile/shared_womp_rat.iff", "object/mobile/shared_gaping_spider.iff",
        "object/mobile/shared_bantha.iff", "object/mobile/shared_cu_pa.iff"};
    auto creatures=dummyclient::resolveCreatureMaterials(SWG_CLIENT_ROOT,paths);
    REQUIRE(creatures.size()==6);
    const std::vector<size_t> expectedCounts={7,8,1,2,3,4};
    size_t total=0;
    for(size_t i=0;i<creatures.size();++i) {
        const auto& creature=creatures[i];REQUIRE(creature.submeshes.size()==expectedCounts[i]);
        // Independently reparse the source MGN, in shader-group order.
        std::map<std::string,size_t> nextGroup;
        for(const auto& part:creature.submeshes) {
            auto bytes=fixtures().extract(part.meshFilename);REQUIRE(bytes);
            auto source=assets::SkeletalMesh::parse(*bytes);
            auto group=nextGroup[part.meshFilename]++;REQUIRE(group<source.submeshes.size());
            CHECK(part.shaderFilename==source.submeshes[group].shaderFilename);
            checkGeometry(part.mesh,source.submeshes[group]);
            auto sht=fixtures().extract(part.shaderFilename);REQUIRE(sht);
            auto expectedTexture=assets::ShaderTemplate::parse(*sht).mainTextureFilename;
            CHECK(part.textureFilename==expectedTexture);REQUIRE(part.texture);
            auto dds=fixtures().extract(expectedTexture);REQUIRE(dds);
            auto expected=assets::DdsImage::parse(*dds);
            CHECK(part.texture->width==expected.width);CHECK(part.texture->height==expected.height);
            CHECK(part.texture->format==expected.format);CHECK(part.texture->blockData==expected.blockData);
            ++total;
        }
    }
    CHECK(total==25);
    CHECK(creatures[3].submeshes[0].textureFilename=="texture/gaping_spider_red_a.dds");
    CHECK(creatures[3].submeshes[1].textureFilename=="texture/gaping_spider_red_b.dds");
    CHECK(creatures[0].submeshes[5].textureFilename=="texture/hum_b_eye.dds");
}
