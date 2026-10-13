#pragma once
#include <cstdint>
#include "clientcommon/CreatureStatus.h"
#include <optional>
#include <span>
#include <string>

namespace clientcommon {
struct TargetPoint { float x=0,y=0,z=0; };
struct InspectionTarget {
    uint64_t id=0;
    std::u16string name;
    TargetPoint position;
    bool resolved=false;
    bool self=false;
    CreatureStatus status;
};

// Local inspection only. No server target/command packet is submitted.
// Callers supply creature records and the same positions used for drawing.
class TargetSelection {
public:
    static constexpr float rangeMeters=64.f;
    uint64_t id() const { return id_; }
    void clear() { id_=0; }
    void reconcile(std::span<const InspectionTarget> targets);
    void cycle(std::span<const InspectionTarget> targets,TargetPoint self);
    void pick(std::span<const InspectionTarget> targets,TargetPoint self,
              TargetPoint rayOrigin,TargetPoint rayDirection);
    const InspectionTarget* selected(std::span<const InspectionTarget> targets) const;
    static std::optional<float> distance(const InspectionTarget& target,TargetPoint self);
private:
    uint64_t id_=0;
};

// Bound UI text and remove controls without splitting surrogate pairs.
std::u16string targetDisplayName(const std::u16string& name);
std::wstring targetWideText(const std::u16string& text);
std::string targetDistanceText(std::optional<float> distance);
} // namespace clientcommon
