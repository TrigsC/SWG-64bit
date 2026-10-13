#include "MaterialResolver.h"
#include "assets/ShaderTemplate.h"
#include <exception>
#include <utility>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>

namespace dummyclient {
std::optional<assets::DdsImageData> resolveSubmeshTexture(
    const std::string& shaderFilename,
    const AssetExtractor& tryExtract, std::string* textureFilename) {
    if (textureFilename) textureFilename->clear();
    if (shaderFilename.empty()) {
        return std::nullopt;
    }
    std::optional<std::vector<uint8_t>> shtBytes;
    try { shtBytes = tryExtract(shaderFilename); }
    catch (const std::exception&) { return std::nullopt; }
    if (!shtBytes.has_value()) {
        return std::nullopt;
    }
    assets::ShaderTemplateData sht;
    try {
        sht = assets::ShaderTemplate::parse(*shtBytes);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (sht.mainTextureFilename.empty()) {
        return std::nullopt;
    }
    std::optional<std::vector<uint8_t>> ddsBytes;
    try { ddsBytes = tryExtract(sht.mainTextureFilename); }
    catch (const std::exception&) { return std::nullopt; }
    if (!ddsBytes.has_value()) {
        return std::nullopt;
    }
    try {
        auto image = assets::DdsImage::parse(*ddsBytes);
        if (textureFilename) *textureFilename = sht.mainTextureFilename;
        return image;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ResolvedSubmesh resolveSkeletalSubmesh(assets::SkeletalMeshSubmesh submesh,
    const AssetExtractor& extract) {
    ResolvedSubmesh result;
    result.mesh.positions = std::move(submesh.positions);
    result.mesh.normals = std::move(submesh.normals);
    result.mesh.uv0 = std::move(submesh.uv0);
    result.mesh.indices = std::move(submesh.indices);
    result.shaderFilename = std::move(submesh.shaderFilename);
    result.texture = resolveSubmeshTexture(result.shaderFilename, extract, &result.textureFilename);
    return result;
}
void writeCreatureMaterialAudit(const std::vector<CreatureMaterials>& materials,
    const std::string& outputFilename) {
    std::ofstream out(outputFilename);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << "{\"source\":\"production_creature_resolver\",\"server_contacted\":false,\"templates\":[";
    bool firstTemplate = true;
    for (const auto& creature : materials) {
        if (!firstTemplate) out << ',';
        firstTemplate = false;
        out << "{\"template\":" << std::quoted(creature.templateFilename) << ",\"submeshes\":[";
        bool firstSubmesh = true;
        for (const auto& part : creature.submeshes) {
            const auto& m = part.mesh;
            if (m.positions.empty() || m.positions.size() != m.normals.size() || m.positions.size() != m.uv0.size() || m.indices.size() % 3)
                throw std::runtime_error("Invalid material geometry: " + part.meshFilename);
            for (auto index : m.indices) if (index >= m.positions.size()) throw std::runtime_error("Material index outside vertex buffer");
            for (auto uv : m.uv0) if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) throw std::runtime_error("Nonfinite material UV");
            if (!firstSubmesh) out << ',';
            firstSubmesh = false;
            out << "{\"mesh\":" << std::quoted(part.meshFilename) << ",\"shader\":" << std::quoted(part.shaderFilename)
                << ",\"texture\":" << std::quoted(part.textureFilename) << ",\"vertices\":" << m.positions.size()
                << ",\"uvs\":" << m.uv0.size() << ",\"indices\":" << m.indices.size();
            if (part.texture) {
                const auto& t = *part.texture;
                out << ",\"width\":" << t.width << ",\"height\":" << t.height
                    << ",\"format\":" << std::quoted(t.format == assets::DdsBlockFormat::Bc1 ? "BC1" : t.format == assets::DdsBlockFormat::Bc2 ? "BC2" : "BC3")
                    << ",\"base_mip_bytes\":" << t.blockData.size();
            }
            out << '}';
        }
        out << "]}";
    }
    out << "]}";
}

}
