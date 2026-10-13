#pragma once
#include <optional>
#include "swgproto/TangibleObjectBaseline3.h"
#include "swgproto/TangibleObjectBaseline6.h"
#include "worldmodel/WorldObject.h"

namespace worldmodel {
// Ground-scene POB parent only. SHIP BASE3's first 11 fields and BASE6's
// first 2 fields match the existing tangible schemas (Core3 pinned in the
// containment audit). Keep the wire type SHIP; do not treat the rest of the
// ship baseline, delta groups, flight or combat state as decoded.
struct ShipObject : WorldObject {
    std::optional<swgproto::TangibleObjectBaseline3> tangible3Prefix;
    std::optional<swgproto::TangibleObjectBaseline6> tangible6Prefix;
};
} // namespace worldmodel
