#pragma once
#include "worldmodel/WorldObject.h"
#include "assets/BuildingLayout.h"
#include <optional>
#include <unordered_map>
namespace worldmodel {
struct Quaternion {float x=0,y=0,z=0,w=1;};
std::optional<Quaternion> normalizedRotation(Quaternion);
Quaternion headingRotation(float radians);
Quaternion multiplyRotation(Quaternion parent,Quaternion local);
Quaternion inverseRotation(Quaternion);
assets::Float3 rotateVector(Quaternion,assets::Float3);
float rotationHeading(Quaternion);
// Quaternion callers use normalizedRotation before constructing a frame.
struct SpatialTransform {
    assets::Float3 origin;
    Quaternion rotation;
    assets::Float3 toWorld(assets::Float3) const;
    assets::Float3 toLocal(assets::Float3) const;
    bool preservesWorldUp(float tolerance=1e-5f) const;
};
// A newer compact update supersedes an earlier high-resolution orientation.
// Malformed selected quaternion data is unresolved, never silently replaced.
std::optional<Quaternion> objectRotation(const WorldObject&);
std::optional<SpatialTransform> objectLocalTransform(const WorldObject&);
std::optional<SpatialTransform> resolveWorldTransform(
    const std::unordered_map<uint64_t,WorldObject>& snapshot,const WorldObject&);
} // namespace worldmodel
