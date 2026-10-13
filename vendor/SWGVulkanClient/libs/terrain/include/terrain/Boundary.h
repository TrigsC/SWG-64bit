#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "assets/IffReader.h"
#include "assets/StaticMesh.h"
#include "terrain/LayerItem.h"

namespace terrain {

// Real boundary tags: BCIR (circle), BREC (rectangle), BPOL (polygon), BPLN
// (polyline). BALL/BSPL are dead tags still present in some real files'
// generator streams (the real engine enters then unconditionally skips
// them, never constructing an object) - recognized by isBoundaryTag() but
// parseBoundary() returns std::nullopt for them, matching that behavior.
//
// Supported versions: BCIR/0002, BREC/0002 and patched BREC/0003,
// BPOL/0005, BPLN/0001. The original base-archive scan (a
// real-bytes scan of all 2054 layers across all 10 classic .trn files
// found zero exceptions). Pre-CU patch_01.tre adds BREC/0003 with the same
// rectangle/feather prefix; its extra metadata is not represented here.
// parseBoundary() throws on other versions rather than guessing.
struct BoundaryCircle {
    float centerX = 0.0f;
    float centerZ = 0.0f;
    float radius = 0.0f;
    int featherFunction = 0;
    float featherDistance = 0.0f;
};

struct BoundaryRectangle {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    int featherFunction = 0;
    float featherDistance = 0.0f;
};

struct BoundaryPolygon {
    std::vector<assets::Float2> points;
    int featherFunction = 0;
    float featherDistance = 0.0f;
    bool localWaterTable = false;
    float localWaterTableHeight = 0.0f;
    float localWaterTableShaderSize = 0.0f;
    std::string localWaterTableShaderTemplateName;
};

struct BoundaryPolyline {
    std::vector<assets::Float2> points;
    int featherFunction = 0;
    float featherDistance = 0.0f;
    float width = 2.0f;
};

using BoundaryShape =
    std::variant<BoundaryCircle, BoundaryRectangle, BoundaryPolygon, BoundaryPolyline>;

struct Boundary {
    LayerItemHeader header;
    BoundaryShape shape;
};

bool isBoundaryTag(uint32_t tag);

// `form` is the boundary's own FORM node (e.g. `FORM BCIR`). Returns
// std::nullopt for the dead BALL/BSPL tags. Throws std::runtime_error for
// an unrecognized tag or an unsupported version.
std::optional<Boundary> parseBoundary(const assets::IffChunk& form);

} // namespace terrain
