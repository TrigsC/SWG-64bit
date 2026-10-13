#pragma once
#include "assets/BuildingLayout.h"
#include <optional>
#include <cstddef>

namespace worldmodel {
// Exterior support only. True interior cells retain their floor/portal path;
// terrain is never passed to that path. Limits are a native stepping policy,
// not constants established from the original Windows controller.
struct ExteriorFloorLimits {
    float stepUp = .75f;
    float dropWithoutTerrain = 1.5f;
    float edgeSlack = .001f; // metres, not an area-dependent barycentric margin
};
struct ExteriorFloorSupport {
    enum class Source { Hold, Terrain, FloorMesh, CollisionMesh };
    float height = 0;
    Source source = Source::Hold;
    std::optional<size_t> triangle;
    size_t projectedCandidates = 0, buriedCandidates = 0, unreachableCandidates = 0;
};
ExteriorFloorSupport queryExteriorFloorSupport(
    const assets::FloorCollisionMesh& floor, const assets::MeshData& collision,
    assets::Float3 position, std::optional<float> terrainHeight,
    ExteriorFloorLimits limits = {});
const char* exteriorFloorSourceName(ExteriorFloorSupport::Source source);
} // namespace worldmodel
