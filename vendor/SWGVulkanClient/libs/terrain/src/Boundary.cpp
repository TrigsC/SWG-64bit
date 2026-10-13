#include "terrain/Boundary.h"

#include <stdexcept>

#include "GeneratorItem.h"
#include "Tags.h"

namespace terrain {

namespace {

Boundary parseBoundaryCircle(const assets::IffChunk& form) {
    VersionedPayload v = enterVersioned(form, versionTag(2), "BoundaryCircle");
    BoundaryCircle shape;
    shape.centerX = v.data.readFloat();
    shape.centerZ = v.data.readFloat();
    shape.radius = v.data.readFloat();
    shape.featherFunction = static_cast<int>(v.data.readUint32());
    shape.featherDistance = readClampedFeatherDistance(v.data);
    return Boundary{v.header, shape};
}

Boundary parseBoundaryRectangle(const assets::IffChunk& form) {
    // The Pre-CU patch_01.tre Tatooine file contains 29 BREC/0003 records.
    // Their first 24 bytes retain the rectangle/feather layout of 0002;
    // the trailing metadata is outside this height/color-only renderer.
    const bool version3 = !form.children.empty() && form.children[0].formType == versionTag(3);
    VersionedPayload v = enterVersioned(form, versionTag(version3 ? 3 : 2), "BoundaryRectangle");
    if (version3 && v.data.size() < 41) {
        throw std::runtime_error("terrain: BoundaryRectangle/0003 truncated payload");
    }
    BoundaryRectangle shape;
    shape.x0 = v.data.readFloat();
    shape.y0 = v.data.readFloat();
    shape.x1 = v.data.readFloat();
    shape.y1 = v.data.readFloat();
    shape.featherFunction = static_cast<int>(v.data.readUint32());
    shape.featherDistance = readClampedFeatherDistance(v.data);
    return Boundary{v.header, shape};
}

Boundary parseBoundaryPolygon(const assets::IffChunk& form) {
    VersionedPayload v = enterVersioned(form, versionTag(5), "BoundaryPolygon");
    BoundaryPolygon shape;
    shape.points = readPointList2D(v.data);
    shape.featherFunction = static_cast<int>(v.data.readUint32());
    shape.featherDistance = readClampedFeatherDistance(v.data);
    shape.localWaterTable = v.data.readUint32() != 0;
    shape.localWaterTableHeight = v.data.readFloat();
    shape.localWaterTableShaderSize = v.data.readFloat();
    shape.localWaterTableShaderTemplateName = assets::readNulTerminatedString(v.data);
    return Boundary{v.header, shape};
}

Boundary parseBoundaryPolyline(const assets::IffChunk& form) {
    VersionedPayload v = enterVersioned(form, versionTag(1), "BoundaryPolyline");
    BoundaryPolyline shape;
    shape.points = readPointList2D(v.data);
    shape.featherFunction = static_cast<int>(v.data.readUint32());
    shape.featherDistance = readClampedFeatherDistance(v.data);
    shape.width = v.data.readFloat();
    return Boundary{v.header, shape};
}

} // namespace

bool isBoundaryTag(uint32_t tag) {
    return tag == kTagBALL || tag == kTagBCIR || tag == kTagBREC || tag == kTagBPOL ||
           tag == kTagBSPL || tag == kTagBPLN;
}

std::optional<Boundary> parseBoundary(const assets::IffChunk& form) {
    switch (form.formType) {
        case kTagBALL:
        case kTagBSPL:
            // Dead tags - the real engine enters then unconditionally skips
            // these, never constructing an object.
            return std::nullopt;
        case kTagBCIR:
            return parseBoundaryCircle(form);
        case kTagBREC:
            return parseBoundaryRectangle(form);
        case kTagBPOL:
            return parseBoundaryPolygon(form);
        case kTagBPLN:
            return parseBoundaryPolyline(form);
        default:
            throw std::runtime_error("terrain: parseBoundary called with a non-boundary tag");
    }
}

} // namespace terrain
