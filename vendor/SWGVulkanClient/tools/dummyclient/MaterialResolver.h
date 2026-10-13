#pragma once
#include "assets/StaticMesh.h"
#include "assets/DdsImage.h"
#include "assets/SkeletalMesh.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dummyclient {
using AssetExtractor = std::function<std::optional<std::vector<uint8_t>>(const std::string&)>;
// CPU-only data passed from the asset worker to the render thread.
struct ResolvedSubmesh {
    assets::MeshData mesh;
    std::optional<assets::DdsImageData> texture;
    std::string shaderFilename, textureFilename, meshFilename;
};
std::optional<assets::DdsImageData> resolveSubmeshTexture(
    const std::string& shaderFilename, const AssetExtractor& extract,
    std::string* textureFilename = nullptr);
struct CreatureMaterials {
    std::string templateFilename;
    std::vector<ResolvedSubmesh> submeshes;
};
// Uses the live resolver and configured TRE precedence; no window/network.
std::vector<CreatureMaterials> resolveCreatureMaterials(const std::string& clientPath,
    const std::vector<std::string>& templateFilenames);
void writeCreatureMaterialAudit(const std::vector<CreatureMaterials>& materials,
    const std::string& outputFilename);
ResolvedSubmesh resolveSkeletalSubmesh(assets::SkeletalMeshSubmesh submesh,
    const AssetExtractor& extract);
}
