#pragma once

#include "assets/StaticMesh.h"
#include <cstdint>
#include <span>
#include <vector>

namespace renderer {

// Immutable CPU geometry, built on the asset worker from the same triangles
// that are uploaded for drawing. Independent of character/navmesh collision.
class CameraCollisionMesh {
public:
    struct Hit {
        float fraction = 1;
        bool hit = false;
        bool initialOverlap = false;
        uint32_t triangleTests = 0;
    };
    CameraCollisionMesh(std::span<const assets::Float3> positions,
                        std::span<const uint32_t> indices);
    Hit sweep(assets::Float3 origin, assets::Float3 destination, float radius) const;
    size_t triangleCount() const { return triangles_.size(); }
private:
    struct Triangle { assets::Float3 a, b, c; };
    struct Node {
        assets::Float3 low, high;
        uint32_t begin = 0, count = 0, left = 0, right = 0;
    };
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    uint32_t build(uint32_t begin, uint32_t end);
};

// Sphere encloses all near-plane corners, with a small additional clearance.
// This is a conservative native presentation policy, not Windows parity.
float cameraClearanceRadius(float verticalFov, float aspect, float nearPlane,
                            float clearance = .05f);

// Retract immediately; release with frame-rate-independent exponential easing.
// Always capped by this frame's safe distance, including after yaw/target changes.
// The user's requested zoom remains separate and is never overwritten.
class CameraBoom {
public:
    float update(float safeDistance, float deltaSeconds);
    void reset() { initialized_ = false; }
private:
    bool initialized_ = false;
    float distance_ = 0;
};
} // namespace renderer
