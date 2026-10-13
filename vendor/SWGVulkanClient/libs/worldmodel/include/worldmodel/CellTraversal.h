#pragma once
#include "assets/BuildingLayout.h"
#include <span>
#include <optional>
#include <unordered_map>

namespace worldmodel {
struct ServerCellBinding {
    uint64_t objectId=0,buildingId=0;
    int32_t number=0;
};
// Cell zero is exterior/world space. An interior number needs exactly one
// server cell object in this building; absent/ambiguous mappings are unresolved.
std::optional<uint64_t> resolveCellParent(std::span<const ServerCellBinding>,uint64_t buildingId,size_t cell);
struct CellCrossing {
    uint64_t buildingId=0;
    size_t sourceCell=0,destinationCell=0,shape=0;
    float fraction=0;
};
// Proposals never change the selected cell. A blocked segment cannot commit.
// Signed-side memory handles a step that lands exactly on a portal plane:
// transition is deferred until the next accepted step moves to the other side.
class CellTraversal {
public:
    void select(uint64_t building,size_t cell);
    std::optional<CellCrossing> propose(std::span<const assets::CellPortal>,
        std::span<const assets::PortalShape>,assets::Float3 from,assets::Float3 to,size_t cellCount);
    bool commit(const CellCrossing&,bool movementAccepted);
    size_t cell() const {return cell_;}
private:
    uint64_t building_=0;
    size_t cell_=0;
    std::unordered_map<uint64_t,float> lastSides_;
};
} // namespace worldmodel
