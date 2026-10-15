// The crude wireframe/real-mesh visualizer - Windows/Vulkan only (see
// libs/renderer). Extracted out of main.cpp 2026-07-18 (was ~640 lines
// embedded in a 2200+-line file, the single largest concentration of
// application-level logic in tools/dummyclient) so future rendering work
// (terrain, SUI-driven UI) has somewhere bounded to grow instead of piling
// onto an already-large main.cpp. No behavior changed by this move - see
// SESSION_LOG.md for the extraction itself; every comment below predates
// the move and still describes the same code it always did.
#ifdef SWG_HAS_VISUALIZER

#include "Visualizer.h"
#include <cstdlib>
#include "ClientArchiveOrder.h"
#include "MaterialResolver.h"
#include "TargetHud.h"
#include "clientcommon/LoginInput.h"
#include "clientcommon/ChatLog.h"
#include "clientcommon/ChatText.h"
#include "CommandCatalog.h"
#include "TellCommands.h"
#include "LocalizedChat.h"
#include "ChatObjectState.h"
#include "clientcommon/ChatPresentation.h"
#include "swgproto/CommandQueueEnqueue.h"
#include "clientcommon/MovementReporting.h"
#include "clientcommon/RemoteMotion.h"

#include "AnimationDebugControls.h"
#include "AnimationDiagnosticLogging.h"
#include "PngWriter.h"
#include "creatureanim/AnimationStateSelection.h"
#include "RestPoseAutoTest.h"
#include "ScreenshotCapture.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "animation/SkeletalPose.h"
#include "animation/ActionPlayback.h"
#include "assets/AnimationPriorityMap.h"
#include "assets/AnimationActionMap.h"
#include <random>
#include "assets/AnimationClip.h"
#include "assets/AnimationStateTable.h"
#include "assets/AppearanceTemplate.h"
#include "assets/BuildingLayout.h"
#include "assets/DdsImage.h"
#include "assets/LodFile.h"
#include "assets/SharedObjectTemplate.h"
#include "assets/GameObjectType.h"
#include "assets/ShaderTemplate.h"
#include "assets/SkeletalAppearance.h"
#include "assets/SkeletalMesh.h"
#include "assets/SkeletalMeshLod.h"
#include "assets/Skeleton.h"
#include "assets/StaticMesh.h"
#include "assets/TreArchive.h"
#include "renderer/Camera.h"
#include "renderer/CameraCollision.h"
#include "renderer/VulkanRenderer.h"
#include "renderer/Window.h"
#include "soe/MessageDispatcher.h"
#include "soe/MessageHash.h"
#include "soe/SoeSession.h"
#include "soe/ThreadSafeQueue.h"
#include "swgproto/DataTransform.h"
#include "swgproto/DataTransformWithParent.h"
#include "swgproto/ObjControllerDispatcher.h"
#include "swgproto/ObjectMenuSelect.h"
#include "terrain/Layer.h"
#include "terrain/ProceduralTerrainSource.h"
#include "terrain/TerrainChunkManager.h"
#include "terrain/TerrainGenerator.h"
#include "terrain/TerrainMesh.h"
#include "worldmodel/ObjectStore.h"
#include "worldmodel/PortalVisibility.h"
#include "worldmodel/ExteriorFloorSupport.h"
#include "worldmodel/CellTraversal.h"
#include "worldmodel/SpatialTransform.h"

namespace {

// A rough per-type box size (world units, roughly meters) for the crude
// visualizer - this project has never decoded real model/bounding-box
// dimensions (that's asset-pipeline work, explicitly out of scope for a
// wireframe proof-of-pipeline), so these are hand-picked, honestly
// approximate placeholders, not derived from anything real.
DirectX::XMFLOAT3 visualizerBoxHalfExtentsFor(worldmodel::ObjectTypeTag tag) {
    using worldmodel::ObjectTypeTag;
    switch (tag) {
        case ObjectTypeTag::Creature:
        case ObjectTypeTag::Player:
            return {0.35f, 0.9f, 0.35f}; // tall and narrow - roughly human-sized, standing
        case ObjectTypeTag::Installation:
        case ObjectTypeTag::Static:
            return {5.0f, 2.0f, 5.0f}; // wide, flat footprint - buildings/harvesters/turrets
        case ObjectTypeTag::ResourceContainer:
            return {0.3f, 0.3f, 0.3f};
        case ObjectTypeTag::FactoryCrate:
            return {0.5f, 0.35f, 0.5f}; // crate - wider than tall
        case ObjectTypeTag::Weapon:
            return {0.1f, 0.1f, 0.4f}; // thin and long
        case ObjectTypeTag::Tangible:
        case ObjectTypeTag::Intangible:
        case ObjectTypeTag::Unknown:
        default:
            return {0.5f, 0.5f, 0.5f};
    }
}

DirectX::XMFLOAT4 visualizerColorFor(worldmodel::ObjectTypeTag tag, bool isSelf) {
    using worldmodel::ObjectTypeTag;
    if (isSelf) {
        return {0.2f, 0.9f, 0.9f, 1.0f}; // cyan - stands out regardless of type
    }
    switch (tag) {
        case ObjectTypeTag::Creature:
            return {0.2f, 0.9f, 0.2f, 1.0f}; // green
        case ObjectTypeTag::Player:
            return {0.5f, 0.9f, 0.9f, 1.0f};
        case ObjectTypeTag::Installation:
            return {0.9f, 0.5f, 0.1f, 1.0f}; // orange
        case ObjectTypeTag::ResourceContainer:
            return {0.7f, 0.3f, 0.9f, 1.0f}; // purple
        case ObjectTypeTag::FactoryCrate:
            return {0.6f, 0.4f, 0.2f, 1.0f}; // brown
        case ObjectTypeTag::Weapon:
            return {0.9f, 0.9f, 0.2f, 1.0f}; // yellow
        case ObjectTypeTag::Static:
            return {0.9f, 0.9f, 0.9f, 1.0f}; // white
        case ObjectTypeTag::Tangible:
            return {0.6f, 0.6f, 0.6f, 1.0f}; // gray
        case ObjectTypeTag::Intangible:
            return {0.8f, 0.8f, 0.95f, 1.0f};
        case ObjectTypeTag::Unknown:
        default:
            return {1.0f, 0.1f, 0.1f, 1.0f}; // red - stands out as unexpected
    }
}

// Phase 19 - the debug per-type tint above (green creatures, orange
// installations, etc.) exists to distinguish UNTEXTURED objects from each
// other; multiplying a real, resolved texture by a saturated tint would
// discolor it (a real building rendering green-ish, purple-ish, etc.), which
// defeats the whole point of adding real textures. A textured submesh draws
// with neutral white instead - `texel.rgb * white == texel.rgb` in
// Mesh.hlsl's PSMain, i.e. the real texture shows through unmodified (still
// shaded by the existing half-lambert lighting term). An untextured submesh
// (or the placeholder wireframe box) keeps the debug tint exactly as before.
DirectX::XMFLOAT4 colorForSubmesh(const renderer::MeshHandle& handle,
                                   const DirectX::XMFLOAT4& debugTint) {
    return handle.textureDescriptorSet != VK_NULL_HANDLE
               ? DirectX::XMFLOAT4{1.0f, 1.0f, 1.0f, 1.0f}
               : debugTint;
}

// Resolves an object's real world-space position, accounting for cell-
// relative containment (Phase 17 Step 3 - fixes a real, previously-latent
// bug: this render loop used to treat every object's raw x/y/z as
// world-space unconditionally, regardless of parentId, confirmed via a
// direct grep finding zero occurrences of parentId anywhere in this file
// before this fix). If parentId is set, the object's own x/y/z are LOCAL
// coordinates within its containing building's shared coordinate space -
// NOT a further-nested per-cell transform - confirmed via a real Phase 17
// capture: self's cell-relative coordinates stayed within the same small,
// shared numeric range across every distinct cell of the same building
// (e.g. x/z roughly [-20, 20], y banded by floor), rather than each cell
// having its own independent local origin. Resolved by walking
// parentId (the containing cell's objectId) -> that cell's own containerId
// (Step 2 - the building's objectId) -> the building's own already-tracked
// world position/yaw (the same real position Phase 16 already draws the
// building's mesh at), then rotating+translating the local offset into
// world space using the identical yaw convention predictedSelfPos's own
// facing-vector math uses (forward = (sin(yaw), cos(yaw))). Returns
// nullopt if any link in that chain is missing (parentId!=0 but the cell
// isn't tracked, the cell's own containerId hasn't arrived yet, or the
// building's own position isn't resolved yet) - a REAL, live-caught bug
// fixed here: this used to silently fall back to treating LOCAL x/y/z as
// world-space directly, which is nonsensically wrong (not "close but off",
// actual local coordinates are tiny numbers like (-0.05, 2.75, 11.48) -
// treating those as world-space places the object near the world ORIGIN,
// nowhere near any loaded terrain, looking like it's "ignoring terrain
// height entirely"). Confirmed live: a cell's own UpdateContainmentMessage
// can genuinely never arrive in a given session (observed for the real
// elevator cell after walking in live rather than inheriting a
// server-side position) even though Step 2's permanent-registration fix
// means it isn't a forwarding-kill bug - some cells just don't get resent
// once already established before this client discovered them. Callers
// must handle nullopt by holding the LAST known-good position instead of
// guessing - the same "degrade gracefully, don't guess" bar buildingCache's
// own resolution failures use, just correctly extended to include "we
// don't know yet," not only "we know it isn't a building."
//
// Takes a pre-taken WorldObject SNAPSHOT (ObjectStore::snapshotWorldObjects()),
// NOT the live ObjectStore itself - a real, live-caught bug (Release build:
// "resource deadlock would occur"; Debug build: silently hung) from calling
// ObjectStore::find() for the cell/building lookups below from inside an
// ObjectStore::forEach() callback, which re-locks that store's own
// non-recursive mutex on the same thread it's already held on. Every call
// site takes one snapshot per frame before any forEach() pass, not a fresh
// store lookup per object.
std::optional<DirectX::XMFLOAT3> resolveWorldPosition(
    const std::unordered_map<uint64_t, worldmodel::WorldObject>& snapshot,
    const worldmodel::WorldObject& obj) {
    auto pose=worldmodel::resolveWorldTransform(snapshot,obj);
    if(!pose)return {};
    return DirectX::XMFLOAT3{pose->origin.x,pose->origin.y,pose->origin.z};
}

// Phase 17 Step 5 - real mouse-cursor picking. Unprojects the cursor's
// screen-space position into a world-space ray (origin + normalized
// direction) using the same view/projection matrices the frame is rendered
// with. Two points (near/far plane) are unprojected and the ray direction
// is their difference - the standard technique DirectXMath's own
// XMVector3Unproject() is designed for. World matrix is identity (every
// object position this project tracks is already in world space by the
// time it reaches here).
void screenPointToWorldRay(int screenX, int screenY, int viewportWidth, int viewportHeight,
                            const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
                            DirectX::XMFLOAT3& outOrigin, DirectX::XMFLOAT3& outDirection) {
    DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
    DirectX::XMVECTOR nearPoint = DirectX::XMVector3Unproject(
        DirectX::XMVectorSet(static_cast<float>(screenX), static_cast<float>(screenY), 0.0f, 0.0f),
        0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0f, 1.0f,
        projection, view, world);
    DirectX::XMVECTOR farPoint = DirectX::XMVector3Unproject(
        DirectX::XMVectorSet(static_cast<float>(screenX), static_cast<float>(screenY), 1.0f, 0.0f),
        0.0f, 0.0f, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0f, 1.0f,
        projection, view, world);
    DirectX::XMStoreFloat3(&outOrigin, nearPoint);
    DirectX::XMVECTOR dir =
        DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farPoint, nearPoint));
    DirectX::XMStoreFloat3(&outDirection, dir);
}

// Ray-vs-sphere test, returning the near intersection distance along the
// ray if it exists and is closer than `maxDistance` (which callers shrink
// progressively as closer hits are found, naturally converging on the
// nearest one across a whole-scene scan), else nullopt. Objects have no
// real collision geometry in this project (explicitly out of scope per the
// Phase 17 plan), so picking uses a fixed-radius sphere centered on each
// object's resolved world position - good enough to click on it, not real
// hit-testing against mesh triangles.
std::optional<float> raySphereIntersect(const DirectX::XMFLOAT3& rayOrigin,
                                         const DirectX::XMFLOAT3& rayDir,
                                         const DirectX::XMFLOAT3& sphereCenter, float radius,
                                         float maxDistance) {
    DirectX::XMVECTOR o = DirectX::XMLoadFloat3(&rayOrigin);
    DirectX::XMVECTOR d = DirectX::XMLoadFloat3(&rayDir);
    DirectX::XMVECTOR c = DirectX::XMLoadFloat3(&sphereCenter);
    DirectX::XMVECTOR l = DirectX::XMVectorSubtract(c, o);
    float tca = DirectX::XMVectorGetX(DirectX::XMVector3Dot(l, d));
    if (tca < 0.0f) {
        return std::nullopt;
    }
    float d2 = DirectX::XMVectorGetX(DirectX::XMVector3Dot(l, l)) - tca * tca;
    float r2 = radius * radius;
    if (d2 > r2) {
        return std::nullopt;
    }
    float thc = std::sqrt(r2 - d2);
    float t0 = tca - thc;
    if (t0 < 0.0f) {
        t0 = tca + thc;
    }
    if (t0 < 0.0f || t0 > maxDistance) {
        return std::nullopt;
    }
    return t0;
}

// Real elevator terminal templates carry their direction right in the
// filename (terminal_elevator_up.iff / terminal_elevator_down.iff -
// confirmed in RealMeshResolver's bundled candidate list) - no separate
// wire signal distinguishes them, so picking checks the resolved template
// PATH, not just objectCrc. The plain "terminal_elevator.iff" variant (no
// up/down suffix) is deliberately left unhandled: no real capture exists of
// its actual radialId, and this project doesn't guess wire values it
// hasn't seen (see the project's own "detect and stop, don't guess" rule).
enum class ElevatorDirection { None, Up, Down };

ElevatorDirection elevatorDirectionFor(const std::string* templatePath) {
    if (templatePath == nullptr) {
        return ElevatorDirection::None;
    }
    if (templatePath->find("terminal_elevator_up") != std::string::npos) {
        return ElevatorDirection::Up;
    }
    if (templatePath->find("terminal_elevator_down") != std::string::npos) {
        return ElevatorDirection::Down;
    }
    return ElevatorDirection::None;
}

// Extracts whatever name field a given stored type actually has, mirroring
// dumpObjectStoreSummary()'s own per-type field dispatch exactly (same
// customObjectName/resourceName locations) - this is deliberately NOT a new
// lookup, just reused for a label instead of a console line. Returns nullopt
// for types with no meaningful display name (CellObject/GroupObject aren't
// even drawn, so they never reach this) or whose baseline hasn't decoded yet.
template <typename T>
std::optional<std::u16string> labelTextFor(const T& obj) {
    using worldmodel::CreatureObject;
    using worldmodel::FactoryCrate;
    using worldmodel::InstallationObject;
    using worldmodel::IntangibleObject;
    using worldmodel::PlayerObject;
    using worldmodel::ResourceContainer;
    using worldmodel::StaticObject;
    using worldmodel::TangibleObject;
    using worldmodel::WeaponObject;

    if constexpr (std::is_same_v<T, TangibleObject> || std::is_same_v<T, WeaponObject> ||
                  std::is_same_v<T, IntangibleObject> || std::is_same_v<T, FactoryCrate> ||
                  std::is_same_v<T, StaticObject>) {
        if (obj.base3.has_value() && !obj.base3->customObjectName.empty()) {
            return obj.base3->customObjectName;
        }
    } else if constexpr (std::is_same_v<T, CreatureObject>) {
        if (obj.base3.has_value() && !obj.base3->tangible.customObjectName.empty()) {
            return obj.base3->tangible.customObjectName;
        }
    } else if constexpr (std::is_same_v<T, PlayerObject>) {
        if (obj.base3.has_value() && !obj.base3->intangible.customObjectName.empty()) {
            return obj.base3->intangible.customObjectName;
        }
    } else if constexpr (std::is_same_v<T, InstallationObject>) {
        if (obj.base3.has_value() && !obj.base3->tangible.customObjectName.empty()) {
            return obj.base3->tangible.customObjectName;
        }
    } else if constexpr (std::is_same_v<T, ResourceContainer>) {
        if (obj.base6.has_value() && !obj.base6->resourceName.empty()) {
            return obj.base6->resourceName;
        }
    }
    return std::nullopt;
}

// GDI consumes UTF-16 on Windows; CoreText consumes UTF-32 wchar_t on Mac.
std::wstring toWString(const std::u16string& s) {
    return clientcommon::targetWideText(s);
}

// Real archive files (and, as of 2026-07-18's deed-CRC investigation,
// objectCrc itself) use a "shared_" filename prefix that Core3's own
// Lua-registered template path strings do not include. Originally believed
// to only matter for locating the file inside the .tre archives (see
// RealMeshResolver::resolveUncached() below) - proven wrong by the
// deed-placed-structure investigation: a live-captured objectCrc for a real
// player guild hall (0x6ef998e2) only matches
// soe::MessageHash::compute("object/building/player/shared_player_guildhall_generic_style_01.iff"),
// NOT the un-prefixed form. This means every candidate hashed by
// RealMeshResolver's constructor before this fix was hashed WRONG - not a
// guild-hall-specific bug, a universal one affecting every category's live
// resolution.
std::string toSharedTemplatePath(const std::string& templatePath) {
    size_t slash = templatePath.rfind('/');
    std::string dir = (slash == std::string::npos) ? "" : templatePath.substr(0, slash + 1);
    std::string base = (slash == std::string::npos) ? templatePath : templatePath.substr(slash + 1);
    return dir + "shared_" + base;
}

// One label texture per distinct name string, created on first sight via
// VulkanRenderer::createTextTexture() and reused every frame after - avoids
// re-rasterizing the same name through GDI every single frame.
struct LabelTexture {
    renderer::TextureHandle texture;
    float aspectRatio = 1.0f; // pixelWidth / pixelHeight, for sizing the world-space quad
};

// One real shader-group submesh's geometry plus its resolved real texture
// (Phase 19) - `texture` is nullopt whenever the submesh's own shaderFilename
// is empty, or ANY step of shader/texture resolution fails (missing .sht,
// no MAIN slot, missing .dds, unsupported DDS variant) - never a hard
// failure for the whole submesh, matching this project's established
// per-part skip/fallback convention. Callers upload `mesh` unconditionally
// and `texture` only when present, falling back to the renderer's shared
// white texture otherwise (see MeshHandle::textureDescriptorSet's comment).
using dummyclient::ResolvedSubmesh;
using dummyclient::resolveSubmeshTexture;

// A cell's real axis-aligned bounding box, in the building's own local mesh
// space (the same coordinate space every real cell's raw vertex data is
// already authored in - confirmed no per-cell transform exists, see
// BuildingLayout.h's own comment) - the union of every one of the cell's
// real submeshes' own vertex extents. Computed once at resolve time
// (cheap - the vertex data is already being walked to upload it) and used
// to let self's OWN locally-known position determine which real room it's
// standing in, instead of waiting on a server-reported cellNumber that (a
// real, live-caught gap this session) turns out to only ever update right
// at zone-in or after a genuine server-driven relocation (an elevator
// ride) - ordinary walking between adjacent rooms produces no such update
// at all, since the server has no reason to echo a client's own predicted
// movement back to that same client.
struct CellBounds {
    DirectX::XMFLOAT3 min{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 max{0.0f, 0.0f, 0.0f};
};

std::optional<CellBounds> computeCellBounds(const std::vector<ResolvedSubmesh>& submeshes) {
    std::optional<CellBounds> bounds;
    for (const auto& submesh : submeshes) {
        for (const auto& p : submesh.mesh.positions) {
            if (!bounds.has_value()) {
                bounds = CellBounds{{p.x, p.y, p.z}, {p.x, p.y, p.z}};
                continue;
            }
            bounds->min.x = std::min(bounds->min.x, p.x);
            bounds->min.y = std::min(bounds->min.y, p.y);
            bounds->min.z = std::min(bounds->min.z, p.z);
            bounds->max.x = std::max(bounds->max.x, p.x);
            bounds->max.y = std::max(bounds->max.y, p.y);
            bounds->max.z = std::max(bounds->max.z, p.z);
        }
    }
    return bounds;
}

// One real cell's resolved submeshes plus its own real bounds (see
// CellBounds' own comment) - the unit RealBuildingResolver now resolves per
// cell, and BuildingHandleCache uploads per cell.
//
// collisionMesh (Phase 20) is the real inline CMSH collision geometry
// (assets::BuildingLayout - see BuildingCell::collisionMesh's own comment)
// - deliberately kept as plain CPU-side data here, never uploaded to the
// GPU (unlike `submeshes`), since collision queries run on the CPU every
// frame. May be empty (zero positions) if the real cell had no CMSH data -
// callers must treat that as "no collision geometry for this cell" and
// fall back gracefully, never a hard failure.
struct ResolvedCell {
    std::vector<ResolvedSubmesh> submeshes;
    std::optional<CellBounds> bounds;
    assets::MeshData collisionMesh;
    std::shared_ptr<const renderer::CameraCollisionMesh> cameraMesh;

    // Real portal data (Phase 20b) - this cell's own portal placements
    // (assets::BuildingCell::portals) plus a full copy of the building-wide
    // portalShapes list (assets::BuildingLayoutData::portalShapes,
    // duplicated per cell rather than plumbed through as a separate
    // building-level return value - simpler, and negligible cost: at most
    // a few dozen small polygons per building). See
    // assets::BuildingCell::portals' own comment for the real format.
    std::vector<assets::CellPortal> portals;
    std::vector<assets::PortalShape> portalShapes;

    // Real dedicated floor-collision navmesh (Phase 20c) - see
    // assets::FloorCollisionMesh's own comment. May be empty (zero
    // positions) if the real cell had no real floor-collision filename, or
    // the file failed to load/parse - callers must fall back to
    // collisionMesh's own raycast query for that cell, never a hard
    // failure.
    assets::FloorCollisionMesh floorMesh;
};

// Building model matrices, floor/portal queries and wire reports share one
// normalized quaternion convention. Every live caller validates its frame.
DirectX::XMFLOAT4X4 spatialModelMatrix(const worldmodel::SpatialTransform& frame) {
    auto q=frame.rotation;DirectX::XMFLOAT4X4 matrix;
    DirectX::XMStoreFloat4x4(&matrix,DirectX::XMMatrixRotationQuaternion(DirectX::XMVectorSet(q.x,q.y,q.z,q.w))*
        DirectX::XMMatrixTranslation(frame.origin.x,frame.origin.y,frame.origin.z));
    return matrix;
}
worldmodel::SpatialTransform buildingFrame(const worldmodel::WorldObject& building) {
    auto frame=worldmodel::objectLocalTransform(building);
    if(!frame)throw std::runtime_error("Unresolved building transform");
    return *frame;
}
DirectX::XMFLOAT3 worldToBuildingLocal(const DirectX::XMFLOAT3& p,const worldmodel::WorldObject& building) {
    auto local=buildingFrame(building).toLocal({p.x,p.y,p.z});return {local.x,local.y,local.z};
}
DirectX::XMFLOAT3 buildingLocalToWorld(const DirectX::XMFLOAT3& p,const worldmodel::WorldObject& building) {
    auto world=buildingFrame(building).toWorld({p.x,p.y,p.z});return {world.x,world.y,world.z};
}

// Resolves a real objectCrc (from SceneCreateObjectByCrc, now stored on
// WorldObject - see its own comment) to real uploaded mesh geometry,
// walking the chain this session discovered and verified against real
// data: .iff (SharedObjectTemplate) -> .apt (AppearanceTemplate) -> .lod
// (LodFile, the common case) -> .msh (StaticMesh). Deliberately searches a
// small, FIXED set of client archives (the base archives plus data_sku1_*,
// not every patch archive on the machine) - matches this pass' "minimal"
// scope; content only present in later content patches simply won't
// resolve, falling back to the existing wireframe placeholder box like any
// other resolution failure, never an error that stops rendering. The
// candidate list covers static/tangible/installation/building/intangible/
// weapon/resource_container/factory templates - explicitly NOT mobile/
// creature (skeletal meshes are out of scope this pass, per StaticMesh's
// own name).
class RealMeshResolver {
public:
    explicit RealMeshResolver(const std::string& clientPath) {
        static const char* kArchiveNames[] = {
            "data_other_00.tre",
            "data_static_mesh_00.tre",
            "data_static_mesh_01.tre",
            "data_sku1_00.tre",
            "data_sku1_01.tre",
            "data_sku1_02.tre",
            "data_sku1_03.tre",
            "data_sku1_04.tre",
            "data_sku1_05.tre",
            "data_sku1_06.tre",
            "data_sku1_07.tre",
            // Real .dds texture files (Phase 19) - .sht shader files
            // themselves live in data_other_00.tre above (already opened),
            // but the .dds textures they reference live in these separate
            // archives, confirmed via a direct header dump this session.
            "data_texture_00.tre",
            "data_texture_01.tre",
            "data_texture_02.tre",
            "data_texture_03.tre",
            "data_texture_04.tre",
            "data_texture_05.tre",
            "data_texture_06.tre",
            "data_texture_07.tre",
        };
        for (const auto& name : dummyclient::clientArchiveOrder(clientPath, {std::begin(kArchiveNames),std::end(kArchiveNames)})) {
            std::string path = (std::filesystem::path(clientPath) / name).string();
            try {
                archives_.push_back({name, assets::TreArchive(path)});
                std::cout << "[MESH] opened " << path << " (" << archives_.back().archive.fileCount()
                           << " files)\n";
            } catch (const std::exception& e) {
                std::cerr << "[MESH] failed to open " << path << ": " << e.what() << "\n";
            }
        }

        // Locally generated list of object .iff template paths from this
        // contributor's TRE filename tables. Hashed once here via the CRC-32 algorithm
        // Core3 uses server-side for objectCrc (soe::MessageHash::compute(),
        // confirmed identical to SharedObjectTemplate::getClientObjectCRC()
        // by reading the Core3 reference source directly).
        const char* localAssets = std::getenv("SWG_LOCAL_ASSETS_DIR");
        std::ifstream listFile(std::filesystem::path(localAssets ? localAssets : SWG_ASSETS_DATA_DIR) / "object_template_candidates.txt");
        if (listFile.is_open()) {
            std::string line;
            while (std::getline(listFile, line)) {
                if (line.empty()) {
                    continue;
                }
                // Hash the "shared_"-prefixed form, not the raw Lua-registered
                // path - see toSharedTemplatePath()'s comment for why. The map
                // still stores the RAW path as the value; resolveUncached()
                // re-derives the shared_ form from it for archive lookup.
                uint32_t crc = soe::MessageHash::compute(toSharedTemplatePath(line));
                crcToTemplatePath_[crc] = line;
            }
        }
        std::cout << "[MESH] loaded " << crcToTemplatePath_.size()
                   << " candidate template paths\n";
    }

    // Pure-CPU resolution (Phase 14) - archive extraction + IFF/APT/LOD/MSH
    // parsing, no GPU upload. Callable from the asset worker thread: only
    // touches archives_/crcToTemplatePath_, both immutable after this
    // object's constructor finishes, so a single worker thread calling this
    // repeatedly (never concurrently with itself, and never from the render
    // thread once Phase 14's split is in place) is safe with no locking.
    // Throws on any resolution failure - caller (the worker thread) catches
    // and reports a failed MeshReady, same graceful-degradation-to-
    // placeholder-box behavior as before, just relocated.
    std::vector<ResolvedSubmesh> resolveMeshDataOnly(uint32_t objectCrc) {
        auto candidateIt = crcToTemplatePath_.find(objectCrc);
        if (candidateIt == crcToTemplatePath_.end()) {
            throw std::runtime_error("objectCrc not in the known candidate list");
        }
        const std::string& templatePath = candidateIt->second;
        std::string realIffPath = toSharedTemplatePath(templatePath);

        auto iffBytes = tryExtract(realIffPath);
        if (!iffBytes.has_value()) {
            throw std::runtime_error("template file not found: " + realIffPath);
        }
        auto sharedTemplate = assets::SharedObjectTemplate::parse(*iffBytes);

        auto aptBytes = tryExtract(sharedTemplate.appearanceFilename);
        if (!aptBytes.has_value()) {
            throw std::runtime_error("appearance file not found: " +
                                      sharedTemplate.appearanceFilename);
        }
        auto appearance = assets::AppearanceTemplate::parse(*aptBytes);

        // The .apt's own reference was a .lod for every real case checked
        // this session, but tries the referenced file directly first in
        // case some templates point straight at a .msh instead.
        std::string meshRelativePath;
        if (appearance.referencedFilename.size() >= 4 &&
            appearance.referencedFilename.compare(appearance.referencedFilename.size() - 4, 4,
                                                    ".lod") == 0) {
            auto lodBytes = tryExtract(appearance.referencedFilename);
            if (!lodBytes.has_value()) {
                throw std::runtime_error("lod file not found: " + appearance.referencedFilename);
            }
            auto lod = assets::LodFile::parse(*lodBytes);
            meshRelativePath = "appearance/" + lod.highestDetailMeshFilename;
        } else {
            meshRelativePath = appearance.referencedFilename;
        }

        auto meshBytes = tryExtract(meshRelativePath);
        if (!meshBytes.has_value()) {
            throw std::runtime_error("mesh file not found: " + meshRelativePath);
        }
        // A real mesh almost always has more than one real shader-group
        // submesh (see StaticMeshSubmesh's own comment) - each one becomes
        // its own ResolvedSubmesh, with its own independently-resolved real
        // texture (Phase 19) via resolveSubmeshTexture() - a submesh whose
        // texture fails to resolve still renders (untextured fallback), it
        // just doesn't get one, matching this project's per-part tolerance.
        auto staticSubmeshes = assets::StaticMesh::parse(*meshBytes);
        std::vector<ResolvedSubmesh> result;
        result.reserve(staticSubmeshes.size());
        size_t totalVerts = 0;
        size_t totalTris = 0;
        size_t texturedCount = 0;
        for (auto& submesh : staticSubmeshes) {
            totalVerts += submesh.mesh.positions.size();
            totalTris += submesh.mesh.indices.size() / 3;
            ResolvedSubmesh resolved;
            resolved.texture = resolveSubmeshTexture(
                submesh.shaderFilename,
                [this](const std::string& name) { return tryExtract(name); });
            if (resolved.texture.has_value()) {
                ++texturedCount;
            }
            resolved.mesh = std::move(submesh.mesh);
            result.push_back(std::move(resolved));
        }

        std::cout << "[MESH] resolved objectCrc=0x" << std::hex << objectCrc << std::dec << " -> "
                   << meshRelativePath << " (" << result.size() << " submeshes, " << texturedCount
                   << " textured, " << totalVerts << " verts, " << totalTris << " tris)\n";

        return result;
    }

    // Cheap map lookup only (no archive I/O, no parsing) - safe to call from
    // the render thread every frame, unlike resolveMeshDataOnly() above.
    // Added for Phase 17 Step 5 (real mouse picking): identifying whether a
    // picked object is a known interactable (e.g. an elevator terminal)
    // needs its real template PATH, not just its objectCrc hash, and
    // crcToTemplatePath_ already has exactly that mapping built at
    // construction time.
    const std::string* templatePathFor(uint32_t objectCrc) const {
        auto it = crcToTemplatePath_.find(objectCrc);
        return it != crcToTemplatePath_.end() ? &it->second : nullptr;
    }
    std::optional<uint32_t> templateGameObjectType(uint32_t crc) {
        if(auto found=gameTypes_.find(crc);found!=gameTypes_.end())return found->second;
        const auto* path=templatePathFor(crc);if(!path||gameTypes_.size()>=1024)return {};
        const auto value=assets::gameObjectType(toSharedTemplatePath(*path),[&](const auto& name){return tryExtract(name);});
        gameTypes_.emplace(crc,value);return value;
    }

    // Exposes an already-open archive by name so other resolvers (e.g.
    // TerrainResolver, which also needs data_other_00.tre) can reuse it
    // instead of opening and parsing the same .tre a second time. nullptr
    // if that archive failed to open (see the constructor's own try/catch).
    const assets::TreArchive* findArchiveNamed(const std::string& name) const {
        for (const auto& named : archives_) {
            if (named.name == name) {
                return &named.archive;
            }
        }
        return nullptr;
    }

    std::vector<const assets::TreArchive*> archivePointers() const {
        std::vector<const assets::TreArchive*> result;
        for(const auto& entry:archives_) result.push_back(&entry.archive);
        return result;
    }
    std::optional<std::vector<uint8_t>> extract(std::string name) {std::replace(name.begin(),name.end(),'\\','/');return tryExtract(name);}

private:
    struct NamedArchive {
        std::string name;
        assets::TreArchive archive;
    };

    std::optional<std::vector<uint8_t>> tryExtract(const std::string& name) {
        for (auto& named : archives_) {
            if (named.archive.contains(name)) {
                return named.archive.extract(name);
            }
        }
        return std::nullopt;
    }

    std::vector<NamedArchive> archives_;
    std::unordered_map<uint32_t, std::string> crcToTemplatePath_;
    std::unordered_map<uint32_t,std::optional<uint32_t>> gameTypes_;
};

// Resolves a real objectCrc to real BUILDING geometry (Phase 16) - walks a
// real, distinct chain from RealMeshResolver's own: .iff
// (SharedObjectTemplate) -> .pob (BuildingLayout, a "Portal Object" -
// FORM PRTO) -> one real .msh/.lod per cell (room). Real, confirmed-live
// discovery: buildings leave SharedObjectTemplateData::appearanceFilename
// genuinely EMPTY and set `portalLayoutFilename` instead - a real, direct
// signal in the template data itself (not a guessed type-tag check) for
// which chain to use. Reuses RealMeshResolver's ALREADY-OPEN archives
// (buildings/their cell meshes live in the exact same archive set - no
// second copy opened) and its locally generated candidate list.
class RealBuildingResolver {
public:
    explicit RealBuildingResolver(RealMeshResolver& meshResolver) {
        archives_ = meshResolver.archivePointers();

        const char* localAssets = std::getenv("SWG_LOCAL_ASSETS_DIR");
        const std::filesystem::path localDirectory(localAssets ? localAssets : SWG_ASSETS_DATA_DIR);
        std::ifstream listFile(localDirectory / "object_template_candidates.txt");
        if (listFile.is_open()) {
            std::string line;
            while (std::getline(listFile, line)) {
                if (line.empty()) {
                    continue;
                }
                uint32_t crc = soe::MessageHash::compute(toSharedTemplatePath(line));
                crcToTemplatePath_[crc] = line;
            }
        }
        std::cout << "[BUILDING] loaded " << crcToTemplatePath_.size() << " candidate template paths\n";

        // Optional local terrain-grading map. Community checkouts do not
        // distribute the private server-derived map, so this remains empty
        // unless the contributor supplies their own map under tools/local-assets.
        std::ifstream gradingMapFile(localDirectory / "real_terrain_grading_map.txt");
        if (gradingMapFile.is_open()) {
            std::string line;
            while (std::getline(gradingMapFile, line)) {
                if (line.empty()) {
                    continue;
                }
                size_t tab = line.find('\t');
                if (tab == std::string::npos) {
                    continue;
                }
                realGradingMap_[line.substr(0, tab)] = line.substr(tab + 1);
            }
        }
        std::cout << "[BUILDING] loaded " << realGradingMap_.size() << " real terrain-grading entries\n";
    }

    // Pure-CPU resolution, same threading contract as every other resolver
    // this session (RealMeshResolver/RealSkeletalMeshResolver) - callable
    // from the asset worker thread, throws on any resolution failure for
    // the caller to catch and fall back to the wireframe box. A real
    // building can have a cell whose own mesh fails to resolve without
    // failing the WHOLE building - skipped, not fatal, UNLESS every cell
    // fails, matching RealSkeletalMeshResolver's own per-body-part
    // tolerance.
    // Outer vector: one entry per real cell (same indexing the Phase 17/18
    // draw/filter loop relies on - `buildingCells->size()`/`cellIndex`
    // still mean the same thing). Inner vector: that cell's own real
    // shader-group submeshes, kept SEPARATE (not merged, unlike an earlier
    // version of this pass) so each one gets its own correctly-matched real
    // texture - merging would apply one submesh's texture to every OTHER
    // submesh's geometry too (e.g. a wall showing a floor's marble texture),
    // which is worse than no texture at all.
    std::vector<ResolvedCell> resolveMeshDataOnly(uint32_t objectCrc) {
        auto candidateIt = crcToTemplatePath_.find(objectCrc);
        if (candidateIt == crcToTemplatePath_.end()) {
            throw std::runtime_error("objectCrc not in the known candidate list");
        }
        const std::string& templatePath = candidateIt->second;
        std::string realIffPath = toSharedTemplatePath(templatePath);

        auto iffBytes = tryExtract(realIffPath);
        if (!iffBytes.has_value()) {
            throw std::runtime_error("template file not found: " + realIffPath);
        }
        auto sharedTemplate = assets::SharedObjectTemplate::parse(*iffBytes);
        if (sharedTemplate.portalLayoutFilename.empty()) {
            throw std::runtime_error("not a building (no portalLayoutFilename): " + realIffPath);
        }

        auto pobBytes = tryExtract(sharedTemplate.portalLayoutFilename);
        if (!pobBytes.has_value()) {
            throw std::runtime_error("portal layout file not found: " +
                                      sharedTemplate.portalLayoutFilename);
        }
        auto layout = assets::BuildingLayout::parse(*pobBytes);

        std::vector<ResolvedCell> result;
        result.reserve(layout.cells.size());
        for (const auto& cell : layout.cells) {
            std::vector<ResolvedSubmesh> cellSubmeshResults;
            try {
                // Cell 0 (the exterior shell) real-confirmed to reference a
                // .lod file; every other cell references a real .msh
                // directly - same generic branch RealMeshResolver already
                // uses for ordinary objects, just per-cell here.
                std::string meshPath,appearancePath=cell.modelFilename;
                // A POB shell can reference an APT wrapper, as the real
                // YT-1300 r0 does. Resolve its authored reference before LOD.
                if(appearancePath.ends_with(".apt")) {
                    auto aptBytes=tryExtract(appearancePath);
                    if(!aptBytes)throw std::runtime_error("cell appearance not found: "+appearancePath);
                    appearancePath=assets::AppearanceTemplate::parse(*aptBytes).referencedFilename;
                }
                if (appearancePath.ends_with(".lod")) {
                    auto lodBytes = tryExtract(appearancePath);
                    if (!lodBytes.has_value()) {
                        throw std::runtime_error("lod file not found: " + appearancePath);
                    }
                    auto lod = assets::LodFile::parse(*lodBytes);
                    meshPath = "appearance/" + lod.highestDetailMeshFilename;
                } else {
                    meshPath = appearancePath;
                }

                auto meshBytes = tryExtract(meshPath);
                if (!meshBytes.has_value()) {
                    throw std::runtime_error("cell mesh file not found: " + meshPath);
                }
                // A real cell mesh almost always has more than one real
                // shader-group submesh (confirmed live: the elevator cell
                // alone has 5, not the single 4-triangle platform earlier
                // logs showed) - each becomes its own ResolvedSubmesh with
                // its own independently-resolved real texture.
                auto cellSubmeshes = assets::StaticMesh::parse(*meshBytes);
                size_t totalVerts = 0;
                size_t totalTris = 0;
                size_t texturedCount = 0;
                for (auto& submesh : cellSubmeshes) {
                    totalVerts += submesh.mesh.positions.size();
                    totalTris += submesh.mesh.indices.size() / 3;
                    ResolvedSubmesh resolved;
                    resolved.texture = resolveSubmeshTexture(
                        submesh.shaderFilename,
                        [this](const std::string& name) { return tryExtract(name); });
                    if (resolved.texture.has_value()) {
                        ++texturedCount;
                    }
                    resolved.mesh = std::move(submesh.mesh);
                    cellSubmeshResults.push_back(std::move(resolved));
                }
                // TEMP diagnostic (Phase 17 live debug) - per-cell vertex/
                // triangle counts were never logged before (only the
                // resolved-cell COUNT), so a cell that "resolves" but
                // carries zero/degenerate geometry was invisible in every
                // prior log.
                std::cout << "[BUILDING]   cell \"" << cell.name << "\" -> " << meshPath << " ("
                           << cellSubmeshes.size() << " submeshes, " << texturedCount << " textured, "
                           << totalVerts << " verts, " << totalTris << " tris)\n";
            } catch (const std::exception& e) {
                std::cerr << "[BUILDING] cell \"" << cell.name << "\" failed, skipping: " << e.what()
                           << "\n";
            }
            ResolvedCell resolvedCell;
            resolvedCell.bounds = computeCellBounds(cellSubmeshResults);
            resolvedCell.submeshes = std::move(cellSubmeshResults);
            // Real collision-only geometry (Phase 20) - small (position +
            // index only), a plain copy from the already-parsed layout is
            // fine; `cell` is a const& into `layout.cells` so it can't be
            // moved from here.
            resolvedCell.collisionMesh = cell.collisionMesh;
            // TEMPORARY diagnostic (2026-07-28, ramp collision-gap
            // investigation) - real vertex extents of this cell's own CMSH
            // collision mesh, to check whether it actually covers the real
            // entrance ramp at all (self measured reaching local z~44,
            // vs. the VISUAL submesh bounds only reaching z~32.5) or
            // whether this is a genuine content gap - the collision mesh
            // never having ramp geometry at all, which no margin value
            // could ever work around. Strip once resolved.
            if (!resolvedCell.collisionMesh.positions.empty()) {
                assets::Float3 cmin = resolvedCell.collisionMesh.positions[0];
                assets::Float3 cmax = resolvedCell.collisionMesh.positions[0];
                for (const auto& p : resolvedCell.collisionMesh.positions) {
                    cmin.x = std::min(cmin.x, p.x);
                    cmin.y = std::min(cmin.y, p.y);
                    cmin.z = std::min(cmin.z, p.z);
                    cmax.x = std::max(cmax.x, p.x);
                    cmax.y = std::max(cmax.y, p.y);
                    cmax.z = std::max(cmax.z, p.z);
                }
                std::cout << "[COLLISION DIAG] cell \"" << cell.name << "\" CMSH real extents min=("
                           << cmin.x << "," << cmin.y << "," << cmin.z << ") max=(" << cmax.x << ","
                           << cmax.y << "," << cmax.z << ") vertCount="
                           << resolvedCell.collisionMesh.positions.size() << "\n";
            } else {
                std::cout << "[COLLISION DIAG] cell \"" << cell.name << "\" CMSH is EMPTY\n";
            }
            // Real portal data (Phase 20b) - same reasoning as
            // collisionMesh above (`cell`/`layout` are both const& here).
            resolvedCell.portals = cell.portals;
            resolvedCell.portalShapes = layout.portalShapes;
            // Real dedicated floor-collision navmesh (Phase 20c) - a
            // SEPARATE real file from the inline CMSH data (see
            // assets::FloorCollisionMesh's own comment for why this exists
            // alongside CMSH: it fixes a real switchback-staircase height
            // ambiguity CMSH's own single-raycast query can't). Graceful
            // skip on any failure (missing file, unexpected format) -
            // resolvedCell.floorMesh just stays empty, and callers fall
            // back to the CMSH raycast for that cell, same "degrade, don't
            // fail" posture as everywhere else in this pass.
            if (!cell.floorCollisionFilename.empty()) {
                auto flrBytes = tryExtract(cell.floorCollisionFilename);
                if (flrBytes.has_value()) {
                    try {
                        resolvedCell.floorMesh = assets::FloorCollision::parse(*flrBytes);
                        std::cout << "[FLR] cell \"" << cell.name << "\" -> "
                                   << cell.floorCollisionFilename << " ("
                                   << resolvedCell.floorMesh.positions.size() << " verts, "
                                   << resolvedCell.floorMesh.triangleVertexIndices.size() / 3
                                   << " tris)\n";
                        // TEMPORARY diagnostic - see the matching CMSH one
                        // above.
                        if (!resolvedCell.floorMesh.positions.empty()) {
                            assets::Float3 fmin = resolvedCell.floorMesh.positions[0];
                            assets::Float3 fmax = resolvedCell.floorMesh.positions[0];
                            for (const auto& p : resolvedCell.floorMesh.positions) {
                                fmin.x = std::min(fmin.x, p.x);
                                fmin.y = std::min(fmin.y, p.y);
                                fmin.z = std::min(fmin.z, p.z);
                                fmax.x = std::max(fmax.x, p.x);
                                fmax.y = std::max(fmax.y, p.y);
                                fmax.z = std::max(fmax.z, p.z);
                            }
                            std::cout << "[COLLISION DIAG] cell \"" << cell.name
                                       << "\" FLR real extents min=(" << fmin.x << "," << fmin.y << ","
                                       << fmin.z << ") max=(" << fmax.x << "," << fmax.y << ","
                                       << fmax.z << ")\n";
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "[FLR] cell \"" << cell.name << "\" parse failed: " << e.what()
                                   << "\n";
                    }
                } else {
                    std::cerr << "[FLR] cell \"" << cell.name << "\" file not found: "
                               << cell.floorCollisionFilename << "\n";
                }
            }
            std::vector<assets::Float3> cameraVertices;
            std::vector<uint32_t> cameraIndices;
            for(const auto& part:resolvedCell.submeshes) {
                auto base=uint32_t(cameraVertices.size());
                cameraVertices.insert(cameraVertices.end(),part.mesh.positions.begin(),part.mesh.positions.end());
                for(auto index:part.mesh.indices)cameraIndices.push_back(base+index);
            }
            resolvedCell.cameraMesh=std::make_shared<renderer::CameraCollisionMesh>(cameraVertices,cameraIndices);
            result.push_back(std::move(resolvedCell));
        }

        bool anyCellResolved = false;
        for (const auto& resolvedCell : result) {
            if (!resolvedCell.submeshes.empty()) {
                anyCellResolved = true;
                break;
            }
        }
        if (!anyCellResolved) {
            throw std::runtime_error("no renderable cells resolved for " + realIffPath);
        }

        std::cout << "[BUILDING] resolved objectCrc=0x" << std::hex << objectCrc << std::dec << " -> "
                   << realIffPath << " (" << result.size() << " cells)\n";
        return result;
    }

    // Resolves the real per-building terrain-grading `.lay` file for a
    // building's objectCrc via realGradingMap_ - real GROUND TRUTH, not a
    // heuristic (see realGradingMap_'s own comment for how it was
    // extracted and why an earlier heuristic-guess version of this
    // function was wrong: it applied grading to a player guildhall, but
    // Core3's own real source confirms player construction NEVER gets
    // graded - only military/starport/faction-perk-HQ/lair templates do).
    // Returns std::nullopt (grading simply skipped, never a hard failure)
    // if objectCrc isn't a known building template, the template isn't in
    // the real grading map (the real, correct outcome for every player
    // house/guildhall and the vast majority of other templates), or the
    // mapped `.lay` file is missing/fails to parse.
    std::optional<terrain::TerrainGenerator> resolveGradingLay(uint32_t objectCrc) {
        auto candidateIt = crcToTemplatePath_.find(objectCrc);
        if (candidateIt == crcToTemplatePath_.end()) {
            return std::nullopt;
        }
        // realGradingMap_ is keyed by the real `clientTemplateFileName`
        // form (with the "shared_" filename prefix, matching Core3's own
        // Lua source verbatim) - crcToTemplatePath_ stores the RAW
        // (pre-"shared_") form (see toSharedTemplatePath()'s own comment),
        // so convert before looking up.
        std::string sharedTemplatePath = toSharedTemplatePath(candidateIt->second);
        auto mapIt = realGradingMap_.find(sharedTemplatePath);
        if (mapIt == realGradingMap_.end()) {
            return std::nullopt;
        }
        const std::string& layFile = mapIt->second;

        auto bytes = tryExtract(layFile);
        if (!bytes.has_value()) {
            std::cerr << "[GRADING] real candidate " << layFile << " (for " << sharedTemplatePath
                       << ") not found in any open archive\n";
            return std::nullopt;
        }
        try {
            return terrain::TerrainGenerator::parseStandalone(*bytes);
        } catch (const std::exception& e) {
            std::cerr << "[GRADING] real candidate " << layFile << " failed to parse: " << e.what()
                       << "\n";
            return std::nullopt;
        }
    }

private:
    std::optional<std::vector<uint8_t>> tryExtract(const std::string& name) {
        for (const auto* archive : archives_) {
            if (archive->contains(name)) {
                return archive->extract(name);
            }
        }
        return std::nullopt;
    }

    std::vector<const assets::TreArchive*> archives_;
    std::unordered_map<uint32_t, std::string> crcToTemplatePath_;

    // Optional local map from shared client template path to .lay filename.
    std::unordered_map<std::string, std::string> realGradingMap_;
};

// Real per-body-part CPU-side data needed for animation (Phase 21) - unlike
// RealSkeletalMeshResolver::resolveMeshDataOnly()'s own MeshData (bind-pose
// geometry only, bone weights discarded), this keeps everything a runtime
// skinning pass needs: the real per-shader submeshes (each carrying its own
// `sourceVertexIndices`, see SkeletalMesh.h), this body part's own real
// bone-name list, and its real mesh-level per-vertex bone weights.
struct AnimatedMeshPart {
    std::vector<assets::SkeletalMeshSubmesh> submeshes;
    std::vector<std::string> boneNames;
    std::vector<std::vector<assets::BoneWeight>> vertexWeights;
};

// Real animation resolution for ONE creature template (Phase 21, v1
// deliberately self-only - see runVisualizer()'s own comment on why other
// visible players/creatures still render bind-pose only this phase).
struct SelfAnimationData {
    assets::SkeletonData skeleton;
    std::vector<AnimatedMeshPart> meshParts;
    assets::AnimationStateTableData stateTable; // real states, empty if resolution failed/no LATX
    // Real gender, derived directly from self's own resolved template path
    // (e.g. "object/creature/player/shared_zabrak_male.iff" - every real
    // player template path checked this session carries a literal
    // "_male"/"_female" suffix) - "male"/"female", or empty if neither
    // marker is present (falls back to the real animation state table's
    // own first gender branch, see AnimationStateTable.h's own comment on
    // AnimationSelectionContext).
    std::string gender;
};

// Resolves a real objectCrc to real SKELETAL mesh geometry (Phase 15) -
// walks the real chain this phase's research discovered and verified
// against real data: .iff (SharedObjectTemplate) -> .sat
// (SkeletalAppearance) -> one or more .lmg (SkeletalMeshLod, per real
// body-part) -> .mgn (SkeletalMesh, may itself have zero or more real
// per-shader submeshes). Unlike RealMeshResolver's candidate list (a
// bundled file mechanically extracted from a Core3 source clone this
// machine doesn't have), this resolver builds its own candidate list by
// enumerating REAL creature/mobile template paths directly out of the
// client archives themselves (TreArchive::listFiles()) - the archives are
// the one source of truth this doesn't depend on an external clone for,
// and every real filename found this way is already in the exact
// "shared_"-prefixed form objectCrc is hashed from (see
// toSharedTemplatePath()'s own comment for why that prefix matters),
// unlike the Lua-derived list which needs that transform applied first.
class RealSkeletalMeshResolver {
public:
    explicit RealSkeletalMeshResolver(const std::string& clientPath) {
        static const char* kArchiveNames[] = {
            "data_other_00.tre",
            "data_animation_00.tre", // real .lat/.ans files (Phase 21, animation)
            "data_skeletal_mesh_00.tre",
            "data_skeletal_mesh_01.tre",
            "data_sku1_00.tre",
            "data_sku1_01.tre",
            "data_sku1_02.tre",
            "data_sku1_03.tre",
            "data_sku1_04.tre",
            "data_sku1_05.tre",
            "data_sku1_06.tre",
            "data_sku1_07.tre",
        };
        for (const auto& name : dummyclient::clientArchiveOrder(clientPath, {std::begin(kArchiveNames),std::end(kArchiveNames)})) {
            std::string path = (std::filesystem::path(clientPath) / name).string();
            try {
                archives_.push_back({name, assets::TreArchive(path)});
                std::cout << "[SKELMESH] opened " << path << " (" << archives_.back().archive.fileCount()
                           << " files)\n";
            } catch (const std::exception& e) {
                std::cerr << "[SKELMESH] failed to open " << path << ": " << e.what() << "\n";
            }
        }

        for (const auto& named : archives_) {
            for (const auto& filePath : named.archive.listFiles()) {
                bool isCreatureTemplate =
                    (filePath.rfind("object/mobile/", 0) == 0 ||
                     filePath.rfind("object/creature/", 0) == 0) &&
                    filePath.size() >= 4 && filePath.compare(filePath.size() - 4, 4, ".iff") == 0;
                if (!isCreatureTemplate) {
                    continue;
                }
                uint32_t crc = soe::MessageHash::compute(filePath);
                crcToTemplatePath_[crc] = filePath;
            }
        }
        std::cout << "[SKELMESH] indexed " << crcToTemplatePath_.size()
                   << " real creature/mobile template paths\n";
    }

    // Immutable index only; archive decoding stays outside the object-store lock.
    // Paths in this index already use the client shared_ template name.
    const std::string* templatePathFor(uint32_t objectCrc) const {
        auto found=crcToTemplatePath_.find(objectCrc);
        return found==crcToTemplatePath_.end()?nullptr:&found->second;
    }

    // Pure-CPU resolution (mirrors RealMeshResolver::resolveMeshDataOnly()'s
    // own shape/threading contract exactly - callable from the asset worker
    // thread, throws on any resolution failure for the caller to catch).
    // A real creature can have multiple body-part meshes (appearance.
    // meshFilenames), each with zero or more real per-shader submeshes
    // (SkeletalMesh::parse()'s own confirmed real anomaly - see
    // SkeletalMesh.h) - a body part that fails to resolve or has zero
    // submeshes is simply skipped, not a hard failure, UNLESS every body
    // part fails, in which case there is nothing renderable and this
    // throws so the caller falls back to the placeholder box.
    std::vector<ResolvedSubmesh> resolveMeshDataOnly(uint32_t objectCrc, const dummyclient::AssetExtractor& extractMaterial) {
        auto candidateIt = crcToTemplatePath_.find(objectCrc);
        if (candidateIt == crcToTemplatePath_.end()) {
            throw std::runtime_error("objectCrc not in the known creature/mobile candidate list");
        }
        const std::string& realIffPath = candidateIt->second;

        auto iffBytes = tryExtract(realIffPath);
        if (!iffBytes.has_value()) {
            throw std::runtime_error("template file not found: " + realIffPath);
        }
        assets::SharedObjectTemplateData sharedTemplate;
        try {
            sharedTemplate = assets::SharedObjectTemplate::parse(*iffBytes);
        } catch (const std::exception& e) {
            throw std::runtime_error("template parse failed for " + realIffPath + ": " + e.what());
        }

        auto satBytes = tryExtract(sharedTemplate.appearanceFilename);
        if (!satBytes.has_value()) {
            throw std::runtime_error("skeletal appearance file not found: " +
                                      sharedTemplate.appearanceFilename);
        }
        auto appearance = assets::SkeletalAppearance::parse(*satBytes);

        std::vector<ResolvedSubmesh> submeshMeshes;
        for (const auto& lmgPath : appearance.meshFilenames) {
            auto lmgBytes = tryExtract(lmgPath);
            if (!lmgBytes.has_value()) {
                std::cerr << "[SKELMESH] body-part .lmg not found, skipping: " << lmgPath << "\n";
                continue;
            }
            assets::SkeletalMeshLodData lod;
            try {
                lod = assets::SkeletalMeshLod::parse(*lmgBytes);
            } catch (const std::exception& e) {
                std::cerr << "[SKELMESH] .lmg parse failed, skipping " << lmgPath << ": " << e.what()
                           << "\n";
                continue;
            }
            auto mgnBytes = tryExtract(lod.highestDetailMeshFilename);
            if (!mgnBytes.has_value()) {
                std::cerr << "[SKELMESH] .mgn not found, skipping: " << lod.highestDetailMeshFilename
                           << "\n";
                continue;
            }
            assets::SkeletalMeshData skelMesh;
            try {
                skelMesh = assets::SkeletalMesh::parse(*mgnBytes);
            } catch (const std::exception& e) {
                std::cerr << "[SKELMESH] .mgn parse failed, skipping " << lod.highestDetailMeshFilename
                           << ": " << e.what() << "\n";
                continue;
            }
            for (auto& submesh : skelMesh.submeshes) {
                auto resolved = dummyclient::resolveSkeletalSubmesh(std::move(submesh), extractMaterial);
                resolved.meshFilename = lod.highestDetailMeshFilename;
                submeshMeshes.push_back(std::move(resolved));
            }
        }

        if (submeshMeshes.empty()) {
            throw std::runtime_error("no renderable submeshes resolved for " + realIffPath);
        }

        std::cout << "[SKELMESH] resolved objectCrc=0x" << std::hex << objectCrc << std::dec << " -> "
                   << realIffPath << " (" << submeshMeshes.size() << " submeshes)\n";
        return submeshMeshes;
    }

    // Real animation resolution (Phase 21) - same real .iff -> .sat chain as
    // resolveMeshDataOnly() above, but keeps the full per-body-part
    // submesh/bone-weight data AND additionally resolves the real skeleton
    // (.skt) and animation state table (.lat, via the .sat's own LATX
    // chunk). Called once, synchronously (not via AssetWorkerThread - a
    // one-time few-ms cost for a single object is fine, unlike the
    // streaming-many-creatures case the async pipeline exists for). Returns
    // nullopt on any hard failure (no candidate template, .iff/.sat/.skt
    // unreadable) - a missing/unreadable LATX or individual body part is
    // tolerated (empty stateTable / that part just skipped), matching this
    // project's established per-part graceful-fallback convention.
    std::optional<SelfAnimationData> resolveSelfAnimationData(uint32_t objectCrc) {
        auto candidateIt = crcToTemplatePath_.find(objectCrc);
        if (candidateIt == crcToTemplatePath_.end()) {
            return std::nullopt;
        }
        const std::string& realIffPath = candidateIt->second;

        auto iffBytes = tryExtract(realIffPath);
        if (!iffBytes.has_value()) {
            return std::nullopt;
        }
        assets::SharedObjectTemplateData sharedTemplate;
        try {
            sharedTemplate = assets::SharedObjectTemplate::parse(*iffBytes);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        auto satBytes = tryExtract(sharedTemplate.appearanceFilename);
        if (!satBytes.has_value()) {
            return std::nullopt;
        }
        assets::SkeletalAppearanceData appearance;
        try {
            appearance = assets::SkeletalAppearance::parse(*satBytes);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        auto sktBytes = tryExtract(appearance.skeletonFilename);
        if (!sktBytes.has_value()) {
            return std::nullopt;
        }
        SelfAnimationData result;
        if (realIffPath.find("_female") != std::string::npos) {
            result.gender = "female";
        } else if (realIffPath.find("_male") != std::string::npos) {
            result.gender = "male";
        }
        try {
            result.skeleton = assets::Skeleton::parse(*sktBytes);
        } catch (const std::exception& e) {
            std::cerr << "[ANIM] skeleton parse failed for " << appearance.skeletonFilename << ": "
                       << e.what() << "\n";
            return std::nullopt;
        }

        for (const auto& lmgPath : appearance.meshFilenames) {
            auto lmgBytes = tryExtract(lmgPath);
            if (!lmgBytes.has_value()) continue;
            assets::SkeletalMeshLodData lod;
            try {
                lod = assets::SkeletalMeshLod::parse(*lmgBytes);
            } catch (const std::exception&) {
                continue;
            }
            auto mgnBytes = tryExtract(lod.highestDetailMeshFilename);
            if (!mgnBytes.has_value()) continue;
            assets::SkeletalMeshData skelMesh;
            try {
                skelMesh = assets::SkeletalMesh::parse(*mgnBytes);
            } catch (const std::exception&) {
                continue;
            }
            if (skelMesh.submeshes.empty()) continue;

            AnimatedMeshPart part;
            part.submeshes = std::move(skelMesh.submeshes);
            part.boneNames = std::move(skelMesh.boneNames);
            part.vertexWeights = std::move(skelMesh.vertexWeights);
            result.meshParts.push_back(std::move(part));
        }

        if (!appearance.latFilename.empty()) {
            auto latBytes = tryExtract(appearance.latFilename);
            if (latBytes.has_value()) {
                try {
                    result.stateTable = assets::AnimationStateTable::parse(*latBytes);
                } catch (const std::exception& e) {
                    std::cerr << "[ANIM] .lat parse failed for " << appearance.latFilename << ": "
                               << e.what() << "\n";
                }
            }
        }

        std::cout << "[ANIM] resolved self animation data: " << realIffPath << " -> "
                   << result.meshParts.size() << " body parts, " << result.skeleton.bones.size()
                   << " bones, " << result.stateTable.states.size() << " real animation states\n";
        // Phase 21 diagnostic - real skeleton bone names, one-time dump, to
        // directly compare against a real clip's own XFIN bone names rather
        // than assume they match.
        std::cout << "[ANIMDBG] skeleton bone names: ";
        for (size_t i = 0; i < result.skeleton.bones.size(); ++i) {
            std::cout << "[" << i << "]" << result.skeleton.bones[i].name << " ";
        }
        std::cout << "\n";
        // Phase 21 diagnostic - real bone hierarchy (name + parentIndex,
        // resolved to the parent's own name) - added while investigating
        // the finger "sail" artifact, to directly verify the real skeleton
        // parses the deepest chains (thumb/index/ring) with correct parent
        // links, not just that a plausible-looking name list exists.
        std::cout << "[ANIMDBG] skeleton hierarchy:\n";
        for (size_t i = 0; i < result.skeleton.bones.size(); ++i) {
            int32_t parent = result.skeleton.bones[i].parentIndex;
            std::string parentName = "(none)";
            if (parent >= 0 && static_cast<size_t>(parent) < result.skeleton.bones.size()) {
                parentName = result.skeleton.bones[static_cast<size_t>(parent)].name;
            }
            std::cout << "  [" << i << "]" << result.skeleton.bones[i].name << " parentIndex=" << parent
                       << " parentName=" << parentName << "\n";
        }
        // Phase 21 diagnostic - real per-body-part mesh bone names (from
        // each .mgn's own XFNM chunk) - compared against the skeleton
        // bone names above to confirm case/naming actually matches for
        // mesh-to-skeleton binding too, not just clip-to-skeleton.
        for (size_t pi = 0; pi < result.meshParts.size(); ++pi) {
            std::cout << "[ANIMDBG] meshPart[" << pi << "] bone names: ";
            for (size_t bi = 0; bi < result.meshParts[pi].boneNames.size(); ++bi) {
                std::cout << "[" << bi << "]" << result.meshParts[pi].boneNames[bi] << " ";
            }
            std::cout << "\n";
        }
        // Phase 21 diagnostic - flags any real triangle whose longest edge,
        // measured in BIND-POSE (unanimated) local space, is suspiciously
        // large relative to typical mesh-part scale - added while
        // investigating the finger "sail" artifact after ruling out
        // rotation decode, bind pose data, composition order, skinning
        // technique (linear vs dual-quaternion), and bone hierarchy: a
        // mis-triangulated mesh (a real index-parsing bug connecting two
        // vertices that shouldn't be connected, e.g. two different
        // fingertips) would be invisible in a static bind-pose render if
        // the wrongly-connected vertices happen to sit close together
        // there, but would stretch into an obvious flat panel once the
        // bones actually separate during animation - and would look
        // identical under any skinning technique, since both just render
        // whatever triangles exist, wrong ones included.
        for (size_t pi = 0; pi < result.meshParts.size(); ++pi) {
            const auto& part = result.meshParts[pi];
            for (size_t si = 0; si < part.submeshes.size(); ++si) {
                const auto& sm = part.submeshes[si];
                float maxEdge = 0.0f;
                size_t maxEdgeTri = 0;
                for (size_t t = 0; t + 2 < sm.indices.size(); t += 3) {
                    uint32_t ia = sm.indices[t];
                    uint32_t ib = sm.indices[t + 1];
                    uint32_t ic = sm.indices[t + 2];
                    if (ia >= sm.positions.size() || ib >= sm.positions.size() || ic >= sm.positions.size())
                        continue;
                    const auto& pa = sm.positions[ia];
                    const auto& pb = sm.positions[ib];
                    const auto& pc = sm.positions[ic];
                    auto dist = [](const assets::Float3& x, const assets::Float3& y) {
                        float dx = x.x - y.x, dy = x.y - y.y, dz = x.z - y.z;
                        return std::sqrt(dx * dx + dy * dy + dz * dz);
                    };
                    float e = std::max({dist(pa, pb), dist(pb, pc), dist(pc, pa)});
                    if (e > maxEdge) {
                        maxEdge = e;
                        maxEdgeTri = t / 3;
                    }
                }
                std::cout << "[ANIMDBG] meshPart[" << pi << "] submesh[" << si << "] triCount="
                           << (sm.indices.size() / 3) << " vertCount=" << sm.positions.size()
                           << " maxBindEdgeLength=" << maxEdge << " (tri#" << maxEdgeTri << ")\n";
            }
        }
        return result;
    }

    // Real .ans clip resolution, used lazily by runVisualizer()'s own clip
    // cache - any archive this resolver already has open (including
    // data_animation_00.tre, added Phase 21) may hold it.
    std::optional<assets::AnimationClipData> resolveAnimationClip(const std::string& ansPath) {
        auto bytes = tryExtract(ansPath);
        if (!bytes.has_value()) return std::nullopt;
        try {
            return assets::AnimationClip::parse(*bytes);
        } catch (const std::exception& e) {
            std::cerr << "[ANIM] .ans parse failed for " << ansPath << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

private:
    struct NamedArchive {
        std::string name;
        assets::TreArchive archive;
    };

    std::optional<std::vector<uint8_t>> tryExtract(const std::string& name) {
        // TEMPORARY debug override (Blender reweighting pipeline live test,
        // 2026-07-24) - loads a single reworked .mgn from disk instead of
        // the archived original, to visually verify a vertex-weight fix
        for (auto& named : archives_) {
            if (named.archive.contains(name)) {
                return named.archive.extract(name);
            }
        }
        return std::nullopt;
    }

    std::vector<NamedArchive> archives_;
    std::unordered_map<uint32_t, std::string> crcToTemplatePath_;
};

// Phase 21 (animation) - real posture/locomotion/mood/weapon -> `.lat`
// state name mapping now lives in creatureanim::stateNameFor (see
// libs/creatureanim/include/creatureanim/AnimationStateSelection.h),
// generalized from this file's own original self-only version.

// Looks up `stateName`'s real selected clip path in `table`, walking the
// state's own real selection tree (gender- and mood-branched - see
// AnimationStateTable.h's own comment) rather than blindly taking
// whichever real clip a naive flatten happened to find first. Empty
// string if the state isn't found.
std::string selfClipPathForState(const assets::AnimationStateTableData& table, const char* stateName,
                                  const assets::AnimationSelectionContext& selectionContext,
                                  bool preferLocomotion) {
    for (const auto& state : table.states) {
        if (state.name == stateName) {
            return assets::selectAnimationClip(state.root, selectionContext, preferLocomotion);
        }
    }
    return {};
}

// Resolves a real terrainName (from CmdStartScene, e.g. "terrain/naboo.trn")
// into a real terrain::TerrainSource - real classic-planet .trn files live
// directly in data_other_00.tre (the same archive RealMeshResolver already
// opens for real mesh templates, and also where this exact terrain file's
// own color-ramp images were confirmed to live this session), not a
// separate texture archive. Takes that already-open archive rather than
// opening its own second copy. Any failure (empty terrainName, archive
// missing, parse throws) returns nullptr - the caller falls back to the
// existing flat reference grid, never blocks rendering, same
// graceful-degradation shape as RealMeshResolver's own resolve().
class TerrainResolver {
public:
    explicit TerrainResolver(RealMeshResolver& resolver) : resolver_(resolver) {}

    std::shared_ptr<terrain::TerrainSource> resolve(const std::string& terrainName) {
        if (terrainName.empty()) {
            return nullptr;
        }
        try {
            auto bytes = resolver_.extract(terrainName);
            if(!bytes) throw std::runtime_error("Terrain not found: "+terrainName);
            auto source = std::make_shared<terrain::ProceduralTerrainSource>(
                terrain::ProceduralTerrainSource::parse(*bytes, nullptr, [&](const std::string& name){return resolver_.extract(name);}));
            std::cout << "[TERRAIN] resolved " << terrainName << "\n";
            return source;
        } catch (const std::exception& e) {
            std::cerr << "[TERRAIN] failed to parse " << terrainName << ": " << e.what() << "\n";
            return nullptr;
        }
    }

private:
    RealMeshResolver& resolver_;
};

// Phase 14 (engine loop redesign) - the asset worker thread. Owns a
// TerrainChunkManager and does BOTH real terrain-chunk generation
// (TerrainSource::generateChunk() - the actual measured ~126ms/chunk cost
// from earlier this session, not just mesh triangulation) and real-mesh
// CPU-side resolution (RealMeshResolver::resolveMeshDataOnly()) off the
// render thread. Deliberately touches NEITHER worldmodel::ObjectStore NOR
// SoeSession/networking - its only inputs are immutable archive/terrain-
// source data (safe for one dedicated thread to query repeatedly with no
// locking, since nothing mutates it after construction) and pure
// computation, so its synchronization surface is limited to the four
// queues below. GPU upload itself stays on the render thread (Vulkan
// command buffer recording isn't set up for multi-threaded use here) -
// this thread only ever produces CPU-side data for the render thread to
// upload.
class AssetWorkerThread {
public:
    // A real ordinary object's mesh almost always has more than one real
    // shader-group submesh (see StaticMeshSubmesh's own comment - confirmed
    // live, e.g. a real elevator platform has 5) - same "carries a whole
    // list" shape as SkeletalMeshReady below, for the identical reason. Each
    // ResolvedSubmesh (Phase 19) carries its own independently-resolved
    // real texture alongside its geometry.
    struct MeshReady {
        uint32_t objectCrc;
        std::optional<std::vector<ResolvedSubmesh>> meshData; // nullopt = resolution failed
    };
    // Phase 15 - a real creature can resolve to more than one submesh
    // (multiple body parts, each with its own real per-shader submeshes -
    // see RealSkeletalMeshResolver's own comment), so unlike MeshReady this
    // carries a whole list; nullopt still means "resolution failed
    // entirely" (falls back to the placeholder box), matching MeshReady's
    // own convention exactly. Geometry, shader identity and decoded MAIN
    // textures stay paired across the worker/render-thread handoff.
    struct SkeletalMeshReady {
        uint32_t objectCrc;
        std::optional<std::vector<ResolvedSubmesh>> submeshData;
    };
    // Phase 16 - a real building resolves to one entry per cell (Phase 19:
    // each a ResolvedCell - that cell's own real submeshes, kept separate so
    // each gets its own correctly-matched real texture, plus its own real
    // bounds for local self-position-based room detection - see
    // ResolvedCell/CellBounds' own comments).
    struct BuildingReady {
        uint32_t objectCrc;
        std::optional<std::vector<ResolvedCell>> cellMeshData;
    };
    struct TerrainReady {
        terrain::ChunkCoord coord;
        terrain::TerrainMeshData meshData;
        std::shared_ptr<const renderer::CameraCollisionMesh> cameraMesh;
    };
    // A building instance's real terrain-grading request - see
    // requestGrading()'s own comment for why this is keyed by objectId, not
    // objectCrc. No "Ready" queue counterpart: grading has no GPU upload
    // step, it's a pure background side effect directly into the shared
    // ProceduralTerrainSource, so nothing needs to report back to the
    // render thread.
    struct GradingRequest {
        uint64_t objectId;
        uint32_t objectCrc;
        float worldX;
        float worldZ;
    };

    AssetWorkerThread(RealMeshResolver& meshResolver, RealSkeletalMeshResolver& skeletalMeshResolver,
                       RealBuildingResolver& buildingResolver,
                       std::shared_ptr<terrain::TerrainSource> terrainSource, int radiusInChunks,
                       float initialX=0,float initialZ=0)
        : meshResolver_(meshResolver), skeletalMeshResolver_(skeletalMeshResolver),
          buildingResolver_(buildingResolver) {
        if (terrainSource) {
            terrainChunkManager_ =
                std::make_unique<terrain::TerrainChunkManager>(terrainSource, radiusInChunks);
            // Terrain grading (structure footprint flattening, see
            // requestGrading()) is specific to the procedural terrain
            // system - not part of the abstract TerrainSource interface,
            // same as Core3's own ProceduralTerrainAppearance::
            // addTerrainModification() isn't a generic TerrainManager
            // concept either. A null result here (a future, different
            // TerrainSource implementation) just means grading requests
            // silently no-op - same graceful-degrade posture as everything
            // else in this class.
            proceduralTerrainSource_ =
                std::dynamic_pointer_cast<terrain::ProceduralTerrainSource>(terrainSource);
        }
        updateSelfPosition(initialX,initialZ);
        thread_ = std::thread([this] { run(); });
    }

    ~AssetWorkerThread() {
        stopRequested_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    AssetWorkerThread(const AssetWorkerThread&) = delete;
    AssetWorkerThread& operator=(const AssetWorkerThread&) = delete;

    // Called from the render thread when a newly-seen objectCrc isn't
    // cached and isn't already pending (see MeshHandleCache below).
    void requestMeshResolve(uint32_t objectCrc) { meshJobs_.push(objectCrc); }

    // Same contract as requestMeshResolve(), for creature/player objects
    // (see SkeletalMeshHandleCache below).
    void requestSkeletalMeshResolve(uint32_t objectCrc) { skeletalMeshJobs_.push(objectCrc); }

    // Same contract again, for building objects (see BuildingHandleCache
    // below).
    void requestBuildingResolve(uint32_t objectCrc) { buildingJobs_.push(objectCrc); }

    // Registers real terrain grading (footprint flattening, see
    // RealBuildingResolver::resolveGradingLay()'s own comment) for a
    // building instance - called once per real objectId the first time it's
    // seen with a successfully-resolved buildingCells, matching
    // requestBuildingResolve()'s own "first-seen" caller discipline. Unlike
    // the resolve-by-objectCrc jobs above, grading is keyed by objectId
    // (per-instance world position), not objectCrc (per-template geometry)
    // - the same building template placed at two different positions needs
    // two independent grading registrations.
    void requestGrading(uint64_t objectId, uint32_t objectCrc, float worldX, float worldZ) {
        gradingJobs_.push(GradingRequest{objectId, objectCrc, worldX, worldZ});
    }

    // Un-registers a building's grading (real structure destroyed) - called
    // once the render loop no longer sees a previously-graded objectId.
    void requestUngrading(uint64_t objectId) { ungradingJobs_.push(objectId); }

    // Called from the render thread once per frame - cheap (just an atomic
    // store), read by this thread's own loop to know where to stream
    // terrain around.
    void updateSelfPosition(float x, float z) {
        selfX_.store(x, std::memory_order_relaxed);
        selfZ_.store(z, std::memory_order_relaxed);
    }

    soe::ThreadSafeQueue<MeshReady> meshReady;
    soe::ThreadSafeQueue<SkeletalMeshReady> skeletalMeshReady;
    soe::ThreadSafeQueue<BuildingReady> buildingReady;
    soe::ThreadSafeQueue<TerrainReady> terrainReady;
    soe::ThreadSafeQueue<terrain::ChunkCoord> terrainEvicted;

private:
    void run() {
        while (!stopRequested_.load()) {
            bool didWork = false;

            if (auto objectCrc = meshJobs_.tryPop()) {
                didWork = true;
                std::optional<std::vector<ResolvedSubmesh>> result;
                try {
                    result = meshResolver_.resolveMeshDataOnly(*objectCrc);
                } catch (const std::exception& e) {
                    std::cout << "[MESH] resolution failed for objectCrc=0x" << std::hex
                               << *objectCrc << std::dec << ": " << e.what() << "\n";
                }
                meshReady.push(MeshReady{*objectCrc, std::move(result)});
            }

            if (auto objectCrc = skeletalMeshJobs_.tryPop()) {
                didWork = true;
                std::optional<std::vector<ResolvedSubmesh>> result;
                try {
                    result = skeletalMeshResolver_.resolveMeshDataOnly(*objectCrc,
                        [&](const std::string& path) { return meshResolver_.extract(path); });
                } catch (const std::exception& e) {
                    std::cout << "[SKELMESH] resolution failed for objectCrc=0x" << std::hex
                               << *objectCrc << std::dec << ": " << e.what() << "\n";
                }
                skeletalMeshReady.push(SkeletalMeshReady{*objectCrc, std::move(result)});
            }

            if (auto objectCrc = buildingJobs_.tryPop()) {
                didWork = true;
                std::optional<std::vector<ResolvedCell>> result;
                try {
                    result = buildingResolver_.resolveMeshDataOnly(*objectCrc);
                } catch (const std::exception& e) {
                    std::cout << "[BUILDING] resolution failed for objectCrc=0x" << std::hex
                               << *objectCrc << std::dec << ": " << e.what() << "\n";
                }
                buildingReady.push(BuildingReady{*objectCrc, std::move(result)});
            }

            if (auto job = gradingJobs_.tryPop()) {
                didWork = true;
                if (proceduralTerrainSource_) {
                    // Template-scoped: the expensive part (extracting +
                    // parsing the real .lay file) only happens once per
                    // distinct objectCrc, cached forever - every instance of
                    // the same building template reuses the parsed,
                    // still-untranslated generator.
                    auto cacheIt = gradingTemplateCache_.find(job->objectCrc);
                    if (cacheIt == gradingTemplateCache_.end()) {
                        cacheIt = gradingTemplateCache_
                                      .emplace(job->objectCrc,
                                               buildingResolver_.resolveGradingLay(job->objectCrc))
                                      .first;
                    }
                    if (cacheIt->second.has_value()) {
                        // Per-instance: copy the template generator, then
                        // translate its boundaries + bake its flatten
                        // height to THIS instance's real world position -
                        // mirrors Core3's ProceduralTerrainAppearance::
                        // addTerrainModification() exactly (translateBoundary
                        // + setHeight using the PRE-modification height
                        // sampled at (x,z)).
                        terrain::TerrainGenerator instanceGenerator = *cacheIt->second;
                        float currentHeight =
                            proceduralTerrainSource_->queryHeight(job->worldX, job->worldZ);
                        for (auto& layer : instanceGenerator.topLevelLayers) {
                            terrain::translateLayerBoundaries(layer, job->worldX, job->worldZ);
                            terrain::bakeLayerHeight(layer, currentHeight);
                        }
                        proceduralTerrainSource_->addTerrainModification(job->objectId,
                                                                          std::move(instanceGenerator));
                        gradedPositions_[job->objectId] = {job->worldX, job->worldZ};
                        if (terrainChunkManager_) {
                            terrainChunkManager_->invalidateChunksOverlapping(
                                job->worldX, job->worldZ, kGradingInvalidationRadiusMeters);
                        }
                        std::cout << "[GRADING] applied objectId=" << job->objectId << " objectCrc=0x"
                                   << std::hex << job->objectCrc << std::dec << " at (" << job->worldX
                                   << ", " << job->worldZ << ")\n";
                    }
                }
            }

            if (auto objectId = ungradingJobs_.tryPop()) {
                didWork = true;
                if (proceduralTerrainSource_) {
                    proceduralTerrainSource_->removeTerrainModification(*objectId);
                    auto posIt = gradedPositions_.find(*objectId);
                    if (posIt != gradedPositions_.end()) {
                        if (terrainChunkManager_) {
                            terrainChunkManager_->invalidateChunksOverlapping(
                                posIt->second.first, posIt->second.second,
                                kGradingInvalidationRadiusMeters);
                        }
                        gradedPositions_.erase(posIt);
                    }
                }
            }

            if (terrainChunkManager_) {
                float x = selfX_.load(std::memory_order_relaxed);
                float z = selfZ_.load(std::memory_order_relaxed);
                // Budgeted (Phase 15): at most one new chunk generated per
                // loop iteration, not the whole streaming radius in one
                // blocking call - see TerrainChunkManager::
                // loadOneMissingChunk()'s own comment for why (a live test
                // showed the old update()-every-iteration call starving
                // this thread's mesh/skeletal-mesh job queues for the
                // whole initial terrain burst, worse in a Debug build).
                if (terrainChunkManager_->loadOneMissingChunk(x, z)) {
                    didWork = true;
                }
                const auto& loaded = terrainChunkManager_->loadedChunks();

                // New chunks: generateChunk() + buildChunkMesh() both
                // already ran (the former inside update() above, the
                // real expensive step) - just diff against what we've
                // already told the render thread about.
                for (const auto& [coord, chunkData] : loaded) {
                    if (knownTerrainCoords_.insert(coord).second) {
                        didWork = true;
                        auto mesh=terrain::buildChunkMesh(chunkData);
                        std::vector<assets::Float3> positions;positions.reserve(mesh.vertices.size());
                        for(const auto& vertex:mesh.vertices)positions.push_back(vertex.position);
                        auto cameraMesh=std::make_shared<renderer::CameraCollisionMesh>(positions,mesh.indices);
                        terrainReady.push(TerrainReady{coord,std::move(mesh),std::move(cameraMesh)});
                    }
                }

                // Evicted chunks: previously known, no longer in the
                // manager's own loaded set (out of streaming radius now).
                for (auto it = knownTerrainCoords_.begin(); it != knownTerrainCoords_.end();) {
                    if (loaded.find(*it) == loaded.end()) {
                        didWork = true;
                        terrainEvicted.push(*it);
                        it = knownTerrainCoords_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            if (!didWork) {
                // Nothing to do this cycle - avoid a hot spin loop.
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    // Real per-building footprint radius isn't known client-side (see
    // RealBuildingResolver::resolveGradingLay()'s own comment on why the
    // real filename mapping is a heuristic) - a generous fixed radius that
    // comfortably covers any real player-house/guildhall `.lay` file's own
    // boundary + feather, confirmed by eye against this project's real
    // player-house footprints, used only to decide which already-generated
    // terrain chunks need to be thrown away and regenerated after a grading
    // change (an overly generous radius just means a few extra harmless
    // chunk regenerations, never a correctness problem).
    static constexpr float kGradingInvalidationRadiusMeters = 64.0f;

    RealMeshResolver& meshResolver_;
    RealSkeletalMeshResolver& skeletalMeshResolver_;
    RealBuildingResolver& buildingResolver_;
    std::unique_ptr<terrain::TerrainChunkManager> terrainChunkManager_;
    std::shared_ptr<terrain::ProceduralTerrainSource> proceduralTerrainSource_;
    std::unordered_set<terrain::ChunkCoord, terrain::ChunkCoordHash> knownTerrainCoords_;
    std::atomic<float> selfX_{0.0f};
    std::atomic<float> selfZ_{0.0f};
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    soe::ThreadSafeQueue<uint32_t> meshJobs_;
    soe::ThreadSafeQueue<uint32_t> skeletalMeshJobs_;
    soe::ThreadSafeQueue<uint32_t> buildingJobs_;
    soe::ThreadSafeQueue<GradingRequest> gradingJobs_;
    soe::ThreadSafeQueue<uint64_t> ungradingJobs_;
    // Worker-thread-only state (never touched by the render thread) - the
    // parsed-but-untranslated per-template `.lay` generator (keyed by
    // objectCrc, cached forever like every other resolver cache in this
    // class) and each currently-graded instance's real world position
    // (keyed by objectId, needed to invalidate the right terrain chunks on
    // removal).
    std::unordered_map<uint32_t, std::optional<terrain::TerrainGenerator>> gradingTemplateCache_;
    std::unordered_map<uint64_t, std::pair<float, float>> gradedPositions_;
};

// Render-thread-side cache of resolved mesh handles (Phase 14) - fed by
// AssetWorkerThread's meshReady queue instead of resolving synchronously
// inline in the draw loop. pending_ avoids re-queuing the same objectCrc
// every frame while its resolution is still in flight on the worker
// thread. Mirrors RealMeshResolver's old meshCache_ ownership/lifetime
// exactly (cache-forever, bounded by the finite candidate-template set),
// just relocated here since the render thread - not the CPU-parsing
// resolver - is what owns real GPU handles now.
// Uploads one ResolvedSubmesh's geometry, then (if it resolved a real
// texture) its texture too, wiring the resulting TextureHandle's descriptor
// set directly into the MeshHandle's own textureDescriptorSet field - the
// shared upload step both MeshHandleCache and BuildingHandleCache use below,
// rather than duplicating the two-call sequence in each.
renderer::MeshHandle uploadResolvedSubmesh(renderer::VulkanRenderer& gfx,
                                            const ResolvedSubmesh& resolved) {
    renderer::MeshHandle handle = gfx.loadStaticMesh(resolved.mesh);
    if (resolved.texture.has_value()) {
        renderer::TextureHandle tex = gfx.loadTexture(*resolved.texture);
        handle.textureDescriptorSet = tex.descriptorSet;
    }
    return handle;
}

class MeshHandleCache {
public:
    const std::vector<renderer::MeshHandle>* get(uint32_t objectCrc, AssetWorkerThread& worker) {
        if (objectCrc == 0) {
            return nullptr;
        }
        auto cached = cache_.find(objectCrc);
        if (cached != cache_.end()) {
            return (cached->second.has_value() && !cached->second->empty()) ? &*cached->second
                                                                              : nullptr;
        }
        if (pending_.insert(objectCrc).second) {
            worker.requestMeshResolve(objectCrc);
        }
        return nullptr; // not resolved yet (or just now requested)
    }

    // Drained once per frame from the render loop, budgeted like terrain
    // uploads (see kMaxAssetUploadsPerFrame).
    void drainReady(AssetWorkerThread& worker, renderer::VulkanRenderer& gfx, int maxPerFrame) {
        for (int i = 0; i < maxPerFrame; ++i) {
            auto ready = worker.meshReady.tryPop();
            if (!ready.has_value()) {
                break;
            }
            pending_.erase(ready->objectCrc);
            std::optional<std::vector<renderer::MeshHandle>> handles;
            if (ready->meshData.has_value()) {
                std::vector<renderer::MeshHandle> uploaded;
                uploaded.reserve(ready->meshData->size());
                for (const auto& resolved : *ready->meshData) {
                    uploaded.push_back(uploadResolvedSubmesh(gfx, resolved));
                }
                handles = std::move(uploaded);
            }
            cache_[ready->objectCrc] = std::move(handles);
        }
    }

private:
    std::unordered_map<uint32_t, std::optional<std::vector<renderer::MeshHandle>>> cache_;
    std::unordered_set<uint32_t> pending_;
};

// Phase 15 - same shape/contract as MeshHandleCache above, except a cached
// entry is a whole LIST of mesh handles (one per real submesh a creature's
// body-part meshes resolved to) rather than a single one, since a real
// skeletal appearance can have multiple body parts each with multiple
// per-shader submeshes. An empty (but present, i.e. `has_value()`) vector
// is a real, valid outcome (see RealSkeletalMeshResolver's own comment on
// zero-submesh meshes) and get() below returns nullptr for it exactly like
// a hard resolution failure - both mean "nothing to draw here," so the
// caller's fallback-to-wireframe-box logic doesn't need to distinguish them.
class SkeletalMeshHandleCache {
public:
    const std::vector<renderer::MeshHandle>* get(uint32_t objectCrc, AssetWorkerThread& worker) {
        if (objectCrc == 0) {
            return nullptr;
        }
        auto cached = cache_.find(objectCrc);
        if (cached != cache_.end()) {
            return (cached->second.has_value() && !cached->second->empty()) ? &*cached->second
                                                                              : nullptr;
        }
        if (pending_.insert(objectCrc).second) {
            worker.requestSkeletalMeshResolve(objectCrc);
        }
        return nullptr; // not resolved yet (or just now requested)
    }

    void drainReady(AssetWorkerThread& worker, renderer::VulkanRenderer& gfx, int maxPerFrame) {
        for (int i = 0; i < maxPerFrame; ++i) {
            auto ready = worker.skeletalMeshReady.tryPop();
            if (!ready.has_value()) {
                break;
            }
            pending_.erase(ready->objectCrc);
            std::optional<std::vector<renderer::MeshHandle>> handles;
            if (ready->submeshData.has_value()) {
                std::vector<renderer::MeshHandle> uploaded;
                uploaded.reserve(ready->submeshData->size());
                for (const auto& submeshData : *ready->submeshData) {
                    uploaded.push_back(uploadResolvedSubmesh(gfx, submeshData));
                }
                size_t textured = 0;
                for (const auto& handle : uploaded) if (handle.textureDescriptorSet != VK_NULL_HANDLE) ++textured;
                std::cout << "[NETWORK] Creature material upload: crc=" << ready->objectCrc
                    << " submeshes=" << uploaded.size() << " textured=" << textured
                    << " fallback=" << uploaded.size()-textured << "\n";
                handles = std::move(uploaded);
            }
            cache_[ready->objectCrc] = std::move(handles);
        }
    }

private:
    std::unordered_map<uint32_t, std::optional<std::vector<renderer::MeshHandle>>> cache_;
    std::unordered_set<uint32_t> pending_;
};

// One real cell's uploaded submesh handles plus its own real bounds -
// carried all the way to the render thread (Phase 19) so the draw loop can
// do its own local self-position-based room detection (see CellBounds' own
// comment for why the server-reported cellNumber alone isn't reliable).
struct CellHandles {
    std::vector<renderer::MeshHandle> submeshes;
    std::optional<CellBounds> bounds;
    // Real collision-only geometry (Phase 20) - CPU-side only, never
    // uploaded to the GPU (unlike `submeshes`); may be empty if the real
    // cell had no CMSH data, in which case collision queries for this cell
    // must fall back gracefully (see ResolvedCell::collisionMesh's comment).
    assets::MeshData collisionMesh;
    std::shared_ptr<const renderer::CameraCollisionMesh> cameraMesh;
    // Real portal data (Phase 20b) - see ResolvedCell::portals' own
    // comment.
    std::vector<assets::CellPortal> portals;
    std::vector<assets::PortalShape> portalShapes;
    // Real dedicated floor-collision navmesh (Phase 20c) - see
    // ResolvedCell::floorMesh's own comment.
    assets::FloorCollisionMesh floorMesh;
};

// A real cell's own mesh geometry doesn't extend out into the shared
// doorway/portal threshold it connects through (real interior meshes stop
// at their own walls) - a zero-margin containment test only registers a
// room change once self has walked all the way PAST that threshold,
// producing a real, live-caught "doesn't draw the room until you're
// already standing in it" lag right at every doorway. Padding each cell's
// own real bounds outward by this much before testing containment closes
// that gap - self is treated as "in" the next room while still mid-doorway,
// not only once fully inside it. SWG world units are ~meters (this
// project's own established convention), so this is roughly a doorway's
// own real width.
constexpr float kCellBoundsMarginMeters = 1.0f;

// Real, live-caught gap (2026-07-28): a real entrance ramp exists
// specifically to let a structure sit on varying/sloped terrain (the
// building adapts to the ground, not vice versa) - it extends outward from
// the building's own compact footprint well past kCellBoundsMarginMeters,
// to actually reach natural grade (real measured data: self's own local Z
// reached 43.98 against a bare cell-0 max.z of 32.5 - the real ramp
// extends >11m past the compact box). A first fix tried widening
// kCellBoundsMarginMeters itself, which is WRONG - that same constant also
// gates wall-blocking/persistent-cell/portal-crossing logic, and widening
// it uniformly let real stair-riser geometry register as wall hits well
// outside the building's real interior, breaking movement that worked
// before (a real, live-caught regression, reverted). This SEPARATE,
// floor-height-fallback-only margin (see its real single call site in the
// main per-frame movement/collision block) is sized from the real
// measurement above with headroom, not guessed.
constexpr float kFloorFallbackMarginMeters = 20.0f;

// Which real cell (by index) `localPos` (already in this building's own
// local mesh space - see worldToBuildingLocal()) falls inside (padded by
// kCellBoundsMarginMeters - see its own comment), or nullopt if none. Index
// 0 (the exterior shell) is deliberately skipped - its own bounds encompass
// the whole building, so it would always "win" otherwise. When more than
// one interior cell's padded bounds contain the point (expected right at a
// shared doorway, by design now), the smallest-volume match wins, on the
// theory that a smaller room's own box is a tighter, more specific fit than
// a large room's box that happens to also extend into the same area.
std::optional<size_t> findContainingCellIndex(const std::vector<CellHandles>& cells,
                                               const DirectX::XMFLOAT3& localPos) {
    std::optional<size_t> best;
    float bestVolume = 0.0f;
    for (size_t i = 1; i < cells.size(); ++i) {
        if (!cells[i].bounds.has_value()) {
            continue;
        }
        const auto& b = *cells[i].bounds;
        if (localPos.x < b.min.x - kCellBoundsMarginMeters ||
            localPos.x > b.max.x + kCellBoundsMarginMeters ||
            localPos.y < b.min.y - kCellBoundsMarginMeters ||
            localPos.y > b.max.y + kCellBoundsMarginMeters ||
            localPos.z < b.min.z - kCellBoundsMarginMeters ||
            localPos.z > b.max.z + kCellBoundsMarginMeters) {
            continue;
        }
        float volume = (b.max.x - b.min.x) * (b.max.y - b.min.y) * (b.max.z - b.min.z);
        if (!best.has_value() || volume < bestVolume) {
            best = i;
            bestVolume = volume;
        }
    }
    return best;
}

// Phase 20 (collision) - standard Moller-Trumbore ray/triangle intersection.
// `dir` must be a unit vector; on a hit, `outT` is the real distance from
// `origin` along `dir` (since `dir` is unit-length, t IS the distance, not
// just a ray parameter needing further scaling). Used for both the
// straight-down floor-height query and the horizontal wall-blocking sweep
// below - same math, different ray direction/purpose.
bool rayTriangleIntersect(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& dir,
                           const assets::Float3& v0, const assets::Float3& v1,
                           const assets::Float3& v2, float& outT) {
    constexpr float kEpsilon = 1e-6f;
    DirectX::XMFLOAT3 edge1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
    DirectX::XMFLOAT3 edge2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
    DirectX::XMFLOAT3 h{dir.y * edge2.z - dir.z * edge2.y, dir.z * edge2.x - dir.x * edge2.z,
                         dir.x * edge2.y - dir.y * edge2.x};
    float a = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;
    if (a > -kEpsilon && a < kEpsilon) {
        return false; // ray parallel to this triangle
    }
    float f = 1.0f / a;
    DirectX::XMFLOAT3 s{origin.x - v0.x, origin.y - v0.y, origin.z - v0.z};
    float u = f * (s.x * h.x + s.y * h.y + s.z * h.z);
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    DirectX::XMFLOAT3 q{s.y * edge1.z - s.z * edge1.y, s.z * edge1.x - s.x * edge1.z,
                         s.x * edge1.y - s.y * edge1.x};
    float v = f * (dir.x * q.x + dir.y * q.y + dir.z * q.z);
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    float t = f * (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
    if (t > kEpsilon) {
        outT = t;
        return true;
    }
    return false;
}

// Real floor height (Phase 20c/d) - a proper 2D (X/Z-projected)
// point-in-triangle query against a cell's own dedicated floor-collision
// navmesh (see assets::FloorCollisionMesh's own comment). Preferred over
// the CMSH raycast below wherever real .flr data is available.
//
// KNOWN LIMITATION, found live: a real switchback staircase's LOWER flight
// can share the exact same X/Z footprint as its own UPPER entrance, viewed
// from directly above - confirmed live, self crossing into a stairwell
// cell for the first time got matched to the bottom-of-stairs triangle
// (Y=2.75) instead of the top (Y~9.5), because both real triangles'
// footprints genuinely overlap in 2D at that specific point. A single
// "does any triangle contain this point" query can't disambiguate that -
// it needs real triangle ADJACENCY (assets::FloorCollisionMesh::
// triangleNeighbors) instead: once on a known-good triangle, only that
// triangle and its real neighbors are ever considered for the NEXT frame's
// position, which is a continuity constraint (self can only ever move to a
// triangle it's truly connected to), not a height heuristic that two
// unrelated overlapping surfaces could fool.
struct FloorHit2D {
    float y = 0.0f;
    size_t triangleIndex = 0;
};

// Tests ONE specific real triangle for 2D (X/Z) containment of localPos,
// returning its real barycentric-interpolated Y if it contains the point.
std::optional<float> testFloorTriangle2D(const assets::FloorCollisionMesh& mesh,
                                          size_t triangleIndex,
                                          const DirectX::XMFLOAT3& localPos) {
    size_t i = triangleIndex * 3;
    if (i + 2 >= mesh.triangleVertexIndices.size()) {
        return std::nullopt;
    }
    const auto& v0 = mesh.positions[mesh.triangleVertexIndices[i]];
    const auto& v1 = mesh.positions[mesh.triangleVertexIndices[i + 1]];
    const auto& v2 = mesh.positions[mesh.triangleVertexIndices[i + 2]];
    // Standard 2D barycentric point-in-triangle test.
    float denom = (v1.z - v2.z) * (v0.x - v2.x) + (v2.x - v1.x) * (v0.z - v2.z);
    if (std::fabs(denom) < 1e-8f) {
        return std::nullopt; // degenerate in this X/Z projection
    }
    float a = ((v1.z - v2.z) * (localPos.x - v2.x) + (v2.x - v1.x) * (localPos.z - v2.z)) / denom;
    float b = ((v2.z - v0.z) * (localPos.x - v2.x) + (v0.x - v2.x) * (localPos.z - v2.z)) / denom;
    float c = 1.0f - a - b;
    constexpr float kEdgeTolerance = -0.01f; // small slack for real edge-adjacent positions
    if (a >= kEdgeTolerance && b >= kEdgeTolerance && c >= kEdgeTolerance) {
        return a * v0.y + b * v1.y + c * v2.y;
    }
    return std::nullopt;
}

// Adjacency-restricted query (the normal, everyday path): tests ONLY
// `triangleIndex` and its up-to-3 real neighbors - the ambiguity-free way
// to continue tracking height across ordinary continuous movement. Returns
// nullopt if self has moved off this local neighborhood entirely (a real
// room transition or a big jump) - callers fall back to the full-scan
// version below in that case.
std::optional<FloorHit2D> queryFloorHeight2DAdjacent(const assets::FloorCollisionMesh& mesh,
                                                      size_t triangleIndex,
                                                      const DirectX::XMFLOAT3& localPos) {
    if (auto y = testFloorTriangle2D(mesh, triangleIndex, localPos)) {
        return FloorHit2D{*y, triangleIndex};
    }
    size_t neighborBase = triangleIndex * 3;
    if (neighborBase + 2 < mesh.triangleNeighbors.size()) {
        for (int k = 0; k < 3; ++k) {
            int32_t neighbor = mesh.triangleNeighbors[neighborBase + k];
            if (neighbor < 0) {
                continue;
            }
            size_t neighborIdx = static_cast<size_t>(neighbor);
            if (auto y = testFloorTriangle2D(mesh, neighborIdx, localPos)) {
                return FloorHit2D{*y, neighborIdx};
            }
        }
    }
    return std::nullopt;
}

// Full-mesh scan (only for a fresh cell entry or a big jump, where no
// current-triangle context exists yet): among ALL real triangles whose 2D
// footprint contains localPos.x/z, picks whichever's real Y is CLOSEST to
// `referenceY` - the caller's own last known-good height (e.g. self's Y in
// the PREVIOUS cell, right at the moment of a portal crossing), which
// correctly disambiguates the real switchback case (the upper entrance,
// near referenceY, over the unrelated lower flight far below it).
std::optional<FloorHit2D> queryFloorHeight2DFullScan(const assets::FloorCollisionMesh& mesh,
                                                      const DirectX::XMFLOAT3& localPos,
                                                      float referenceY) {
    std::optional<FloorHit2D> best;
    float bestDelta = 0.0f;
    size_t triCount = mesh.triangleVertexIndices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        auto y = testFloorTriangle2D(mesh, t, localPos);
        if (!y.has_value()) {
            continue;
        }
        float delta = std::fabs(*y - referenceY);
        if (!best.has_value() || delta < bestDelta) {
            best = FloorHit2D{*y, t};
            bestDelta = delta;
        }
    }
    return best;
}

// Independent closest-point oracle used by the camera asset audit.
DirectX::XMFLOAT3 closestPointOnTriangle3D(const DirectX::XMFLOAT3& p, const assets::Float3& a,
                                            const assets::Float3& b, const assets::Float3& c) {
    float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    float apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
    float d1 = abx * apx + aby * apy + abz * apz;
    float d2 = acx * apx + acy * apy + acz * apz;
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return {a.x, a.y, a.z};
    }
    float bpx = p.x - b.x, bpy = p.y - b.y, bpz = p.z - b.z;
    float d3 = abx * bpx + aby * bpy + abz * bpz;
    float d4 = acx * bpx + acy * bpy + acz * bpz;
    if (d3 >= 0.0f && d4 <= d3) {
        return {b.x, b.y, b.z};
    }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        return {a.x + abx * v, a.y + aby * v, a.z + abz * v};
    }
    float cpx = p.x - c.x, cpy = p.y - c.y, cpz = p.z - c.z;
    float d5 = abx * cpx + aby * cpy + abz * cpz;
    float d6 = acx * cpx + acy * cpy + acz * cpz;
    if (d6 >= 0.0f && d5 <= d6) {
        return {c.x, c.y, c.z};
    }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        return {a.x + acx * w, a.y + acy * w, a.z + acz * w};
    }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b.x + (c.x - b.x) * w, b.y + (c.y - b.y) * w, b.z + (c.z - b.z) * w};
    }
    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    return {a.x + abx * v + acx * w, a.y + aby * v + acy * w, a.z + abz * v + acz * w};
}

// Real floor height (Phase 20) - casts a ray straight down through the real
// collision mesh (see BuildingCell::collisionMesh's own comment) from well
// above `localPos`, returning the highest real surface hit below it, or
// nullopt if the mesh is empty/nothing is hit (a real gap between cell
// vertical bounds and where self happens to be, or a cell with no real
// collision data at all) - callers must fall back gracefully in that case,
// never treat it as an error.
//
// KNOWN LIMITATION, found live (Phase 20b): a real switchback staircase
// (two flights stacked over one another within the same cell) breaks
// "highest hit wins" - once self is on the LOWER flight, this ray still
// passes through the UPPER flight/landing directly overhead first, so
// height snaps back to the upper landing instead of continuing down. A
// "closest to self's current height" alternative was tried live and made
// things WORSE (got stuck re-selecting the same wrong surface every frame
// instead of ever finding the true descending path) - reverted. Real fix
// needs something smarter than a single vertical ray (e.g. also checking
// horizontal proximity/reachability from self's last known-good position,
// not just nearest-by-Y) - not yet implemented, tracked as a known gap.
std::optional<float> queryFloorHeight(const assets::MeshData& collisionMesh,
                                       const DirectX::XMFLOAT3& localPos) {
    if (collisionMesh.positions.empty()) {
        return std::nullopt;
    }
    constexpr float kRayStartAbove = 50.0f; // world units - comfortably above any real room height
    DirectX::XMFLOAT3 origin{localPos.x, localPos.y + kRayStartAbove, localPos.z};
    DirectX::XMFLOAT3 down{0.0f, -1.0f, 0.0f};
    std::optional<float> bestT;
    for (size_t i = 0; i + 2 < collisionMesh.indices.size(); i += 3) {
        const auto& v0 = collisionMesh.positions[collisionMesh.indices[i]];
        const auto& v1 = collisionMesh.positions[collisionMesh.indices[i + 1]];
        const auto& v2 = collisionMesh.positions[collisionMesh.indices[i + 2]];
        float t = 0.0f;
        if (rayTriangleIntersect(origin, down, v0, v1, v2, t)) {
            if (!bestT.has_value() || t < *bestT) {
                bestT = t;
            }
        }
    }
    if (!bestT.has_value()) {
        return std::nullopt;
    }
    return origin.y - *bestT;
}

// Real horizontal wall blocking (Phase 20) - true if the straight segment
// from `from` to `to` (both in the same cell's own local space) crosses any
// real triangle of `collisionMesh`. Callers should test at a height above
// the real floor (see kWallTestHeightMeters below) rather than at foot
// level, so a real floor triangle - which the segment would otherwise
// nearly graze along, numerically unstable - is never mistaken for a wall.
bool segmentBlockedByCollisionMesh(const assets::MeshData& collisionMesh,
                                    const DirectX::XMFLOAT3& from, const DirectX::XMFLOAT3& to) {
    if (collisionMesh.positions.empty()) {
        return false;
    }
    DirectX::XMFLOAT3 delta{to.x - from.x, to.y - from.y, to.z - from.z};
    float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    if (length < 1e-5f) {
        return false;
    }
    DirectX::XMFLOAT3 dir{delta.x / length, delta.y / length, delta.z / length};
    for (size_t i = 0; i + 2 < collisionMesh.indices.size(); i += 3) {
        const auto& v0 = collisionMesh.positions[collisionMesh.indices[i]];
        const auto& v1 = collisionMesh.positions[collisionMesh.indices[i + 1]];
        const auto& v2 = collisionMesh.positions[collisionMesh.indices[i + 2]];
        float t = 0.0f;
        if (rayTriangleIntersect(from, dir, v0, v1, v2, t) && t < length) {
            return true;
        }
    }
    return false;
}

// Waist-to-chest height above the real floor, used to test horizontal
// movement against real wall geometry without the test segment nearly
// grazing along the floor mesh itself (see segmentBlockedByCollisionMesh's
// own comment).
constexpr float kWallTestHeightMeters = 1.0f;

// Phase 20, Step 4 - a small, explicitly hand-maintained list of real
// individually-collidable objects that are NOT structural (no BuildingLayout
// cell/CMSH data applies to them - they're ordinary tangible objects) but
// were confirmed live on Finalizer to block movement anyway. Starts with
// exactly the bazaar and bank terminals, both confirmed live. Deliberately
// NOT derived from Core3's own per-template collision flags (confirmed via
// direct byte dump this session: unset/zero for both of these exact
// objects) or from Core3's exact hardcoded per-class server mechanism
// (CloseObjectsVector::COLLIDABLETYPE, not fully traced) - see the plan's
// own Context for the full reasoning. Extend this list opportunistically as
// more real examples turn up through play-testing; no re-derivation needed.
const std::unordered_set<uint32_t>& curatedCollidableObjectCrcs() {
    static const std::unordered_set<uint32_t> crcs = {
        soe::MessageHash::compute("object/tangible/terminal/shared_terminal_bazaar.iff"),
        soe::MessageHash::compute("object/tangible/terminal/shared_terminal_bank.iff"),
    };
    return crcs;
}

// Curated objects have no real per-cell collision mesh to raycast against
// (they're not building geometry) - approximated instead as a simple
// horizontal blocking circle around the object's own real world position (a
// plain point-to-segment distance test in the X/Z plane, ignoring height,
// since these are all fixed floor-standing objects). Same "coarse but
// graceful" spirit as the rest of this pass, not a precise hull.
constexpr float kCuratedObjectBlockRadiusMeters = 0.75f;

bool segmentBlockedByCuratedObject(const DirectX::XMFLOAT3& objPos, const DirectX::XMFLOAT3& from,
                                    const DirectX::XMFLOAT3& to) {
    float dx = to.x - from.x;
    float dz = to.z - from.z;
    float lengthSq = dx * dx + dz * dz;
    float t = 0.0f;
    if (lengthSq > 1e-8f) {
        t = ((objPos.x - from.x) * dx + (objPos.z - from.z) * dz) / lengthSq;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    float closestX = from.x + dx * t;
    float closestZ = from.z + dz * t;
    float distX = objPos.x - closestX;
    float distZ = objPos.z - closestZ;
    return (distX * distX + distZ * distZ) <=
           (kCuratedObjectBlockRadiusMeters * kCuratedObjectBlockRadiusMeters);
}

// Phase 16 - same shape/contract as SkeletalMeshHandleCache above, for real
// building geometry, one level deeper (Phase 19): each cached entry is a
// list of CellHandles, one per real cell, index-aligned with the .pob's own
// cell order exactly as before.
class BuildingHandleCache {
public:
    const std::vector<CellHandles>* get(uint32_t objectCrc, AssetWorkerThread& worker) {
        if (objectCrc == 0) {
            return nullptr;
        }
        auto cached = cache_.find(objectCrc);
        if (cached != cache_.end()) {
            return (cached->second.has_value() && !cached->second->empty()) ? &*cached->second
                                                                              : nullptr;
        }
        if (pending_.insert(objectCrc).second) {
            worker.requestBuildingResolve(objectCrc);
        }
        return nullptr; // not resolved yet (or just now requested)
    }

    void drainReady(AssetWorkerThread& worker, renderer::VulkanRenderer& gfx, int maxPerFrame) {
        for (int i = 0; i < maxPerFrame; ++i) {
            auto ready = worker.buildingReady.tryPop();
            if (!ready.has_value()) {
                break;
            }
            pending_.erase(ready->objectCrc);
            std::optional<std::vector<CellHandles>> handles;
            if (ready->cellMeshData.has_value()) {
                std::vector<CellHandles> uploadedCells;
                uploadedCells.reserve(ready->cellMeshData->size());
                for (const auto& resolvedCell : *ready->cellMeshData) {
                    CellHandles cellHandles;
                    cellHandles.bounds = resolvedCell.bounds;
                    cellHandles.collisionMesh = resolvedCell.collisionMesh;
                    cellHandles.cameraMesh = resolvedCell.cameraMesh;
                    cellHandles.portals = resolvedCell.portals;
                    cellHandles.portalShapes = resolvedCell.portalShapes;
                    cellHandles.floorMesh = resolvedCell.floorMesh;
                    cellHandles.submeshes.reserve(resolvedCell.submeshes.size());
                    for (const auto& resolved : resolvedCell.submeshes) {
                        cellHandles.submeshes.push_back(uploadResolvedSubmesh(gfx, resolved));
                    }
                    uploadedCells.push_back(std::move(cellHandles));
                }
                handles = std::move(uploadedCells);
            }
            cache_[ready->objectCrc] = std::move(handles);
        }
    }

private:
    std::unordered_map<uint32_t, std::optional<std::vector<CellHandles>>> cache_;
    std::unordered_set<uint32_t> pending_;
};

// Phase 14 (engine loop redesign) - owns SoeSession/MessageDispatcher on a
// dedicated thread, confining ALL socket access (both receive AND send) to
// one thread. This sidesteps needing to reason about standalone Asio's
// thread-safety guarantees for concurrent send/receive from different
// threads entirely - the simplest correct option, not a compromise (see
// GAP_ANALYSIS.md section 0.4 and the Phase 14 plan). The render thread
// never touches `session`/`dispatcher` directly anymore: inbound state
// reaches it exclusively through worldmodel::ObjectStore (thread-safe as of
// Phase 14 step 3), and outbound messages are queued via enqueueSend()
// rather than sent directly.
class NetworkThread {
public:
    NetworkThread(soe::SoeSession& session, soe::MessageDispatcher& dispatcher, std::string journalRoot={})
        : journalRoot_(std::move(journalRoot)), session_(session), dispatcher_(dispatcher) {
        thread_ = std::thread([this] { run(); });
    }

    ~NetworkThread() {
        stopRequested_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    NetworkThread(const NetworkThread&) = delete;
    NetworkThread& operator=(const NetworkThread&) = delete;

    // Queues bytes for this thread to actually send on its next loop
    // iteration - called from the render thread (e.g. outbound movement),
    // never sends directly.
    void enqueueSend(std::vector<uint8_t> payload) {
        if(payload.size()>65536||!outbound_.tryPushBounded(std::move(payload),128)) {
            std::cerr<<"[NETWORK] outbound queue limit exceeded; reconnect required\n";
            failed_.store(true);stopRequested_.store(true);
        }
    }

    bool tryEnqueueCommand(std::vector<uint8_t> payload){
        return !failed_.load()&&!stopRequested_.load()&&payload.size()<=65536&&outbound_.tryPushBounded(std::move(payload),128);
    }
    bool tryEnqueueTells(std::vector<std::vector<uint8_t>> payloads){
        if(failed_.load()||stopRequested_.load())return false;
        for(const auto& payload:payloads)if(payload.size()>65536)return false;
        return outbound_.tryPushBatchBounded(std::move(payloads),128);
    }

    // Mirrors the `bool& failed` signal every other connection-driving
    // function in this file already watches - set here instead when the
    // connection fails on this thread, polled by the render loop once per
    // frame.
    bool failed() const { return failed_.load(); }

private:
    void run() {
        const auto started=std::chrono::steady_clock::now();
        std::ofstream journal;size_t journalRows=0;
        if(!journalRoot_.empty()) {
            try {
                std::filesystem::create_directories(journalRoot_);
                const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                const auto path=std::filesystem::path(journalRoot_)/("network-session-"+std::to_string(stamp)+".jsonl");
                journal.open(path);if(journal)std::cout<<"[NETWORK] Transport diagnostics: "<<path.string()<<"\n";
            }catch(const std::exception&){std::cerr<<"[NETWORK] Transport journal unavailable\n";}
        }
        auto record=[&](const char* event) {
            const auto s=session_.transportStats();
            if(journal&&journalRows<1000) {
                journal<<"{\"event\":\""<<event<<"\",\"elapsed_ms\":"<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()
                    <<",\"sent\":"<<s.sent<<",\"received\":"<<s.received<<",\"net_status_requests\":"<<s.netStatusRequests<<",\"net_status_responses\":"<<s.netStatusResponses
                    <<",\"retries\":"<<s.retries<<",\"acked\":"<<s.acked<<",\"pending\":"<<s.pendingPackets<<",\"buffered\":"<<s.bufferedPackets
                    <<",\"duplicates\":"<<s.duplicates<<",\"out_of_order\":"<<s.outOfOrder<<",\"rejected\":"<<s.rejected<<",\"invalid_packets\":"<<s.invalidPackets
                    <<",\"last_validated_age_ms\":"<<s.lastValidatedAgeMs<<"}\n";journal.flush();++journalRows;
            }
        };
        std::cout<<"[NETWORK] Reliable delivery enabled: 50-packet send window; bounded retry and receive queues.\n";
        record("started");
        auto nextSummary = started + std::chrono::seconds(10);
        while (!stopRequested_.load()) {
            if (std::chrono::steady_clock::now() >= nextSummary) {
                const auto stats = session_.transportStats();
                std::cout << "[NETWORK] transport: sent=" << stats.sent << " received=" << stats.received
                          << " net-status requests=" << stats.netStatusRequests
                          << " responses=" << stats.netStatusResponses
                          << " retries="<<stats.retries<<" acked="<<stats.acked<<" pending="<<stats.pendingPackets
                          << " buffered="<<stats.bufferedPackets<<" duplicates="<<stats.duplicates<<" out-of-order="<<stats.outOfOrder
                          << " invalid="<<stats.invalidPackets<<" last-valid-ms="<<stats.lastValidatedAgeMs<<"\n";
                record("transport");
                nextSummary = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
            std::optional<std::vector<uint8_t>> payload;
            while ((payload = outbound_.tryPop()).has_value()) {
                try {
                    session_.sendMessage(*payload);
                } catch (const std::exception& e) {
                    std::cerr << "[NETWORK] send failed: " << e.what() << "\n";
                    record("send_failed");
                    failed_.store(true);
                    return;
                }
            }

            try {
                // A real (not artificially budget-split) timeout now that
                // this loop no longer shares time with rendering - more
                // responsive to a burst of server traffic than the old
                // single-threaded design's 5ms-per-frame drain window
                // could be, not less.
                auto messages = session_.receiveMessages(std::chrono::milliseconds(50));
                for (auto& msg : messages) {
                    dispatcher_.dispatch(msg);
                }
            } catch (const soe::TimeoutError&) {
                // Normal - just means no traffic arrived this cycle.
            } catch (const soe::DisconnectedError& e) {
                std::cerr << "[NETWORK] disconnected: " << e.what() << "\n";
                record("disconnected");
                failed_.store(true);
                return;
            } catch (const std::exception& e) {
                std::cerr << "[NETWORK] receive failed: " << e.what() << "\n";
                record("receive_failed");
                failed_.store(true);
                return;
            }
        }
        record("stopped");
    }

    std::string journalRoot_;
    soe::SoeSession& session_;
    soe::MessageDispatcher& dispatcher_;
    std::thread thread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> failed_{false};
    soe::ThreadSafeQueue<std::vector<uint8_t>> outbound_;
};

} // namespace

// The crude wireframe visualizer (Windows/Vulkan only - see libs/renderer).
// Proves the whole pipeline end to end: network decode -> ObjectStore ->
// rendering, per the user's explicit request once Movement work landed real
// positions in the Object Model.
//
// Phase 14 (engine loop redesign): networking now runs on its own thread
// (NetworkThread, above) rather than being polled inline in this loop - the
// original single-threaded design (a short-timeout poll interleaved with
// rendering) was deliberate and worked, but the user explicitly chose to
// decouple networking proactively rather than wait for it to become a
// measured bottleneck (see GAP_ANALYSIS.md section 0.4). worldmodel::
// ObjectStore is genuinely shared now (the network thread writes, this
// loop reads every frame) and thread-safe as of Phase 14 step 3 - this
// loop never needs its own locking, ObjectStore's internal mutex covers it.
//
// Despawned objects need no special handling here: SceneDestroyObject's
// handler (registered permanently in main(), see the movement-work comment
// above it) already calls objectStore.remove() the moment a real destroy
// message arrives, and this loop re-reads objectStore fresh every single
// frame rather than keeping its own object list - a removed object simply
// stops being drawn on the very next frame.
//
// Only objects with at least one real transform update
// (transformMessagesSeen > 0) are drawn - an object that decoded a baseline
// but never broadcast a position would otherwise render at a misleading
// (0,0,0). UpdateTransformMessage alone left this true for the self
// character and most stationary objects (this headless client sends no
// movement of its own, so nothing ever triggered a real broadcast for them).
// FIXED 2026-07-18: main() now also wires DataTransform/
// DataTransformWithParent (an "idle sync" Core3 reliably sends for the self
// character right at zone-in, confirmed live even on a fully passive
// connection - see SESSION_LOG.md) into the SAME transformMessagesSeen/
// position fields, so self now renders with no changes needed here. Other
// stationary objects (buildings/terminals that never move AND never
// received their own idle-sync) may still be invisible - not yet confirmed
// either way, a real "what to add next" candidate rather than solved here.
struct OfflineEvidence {
    size_t floorChecks=0,wallChecks=0,animatedVertices=0;
    float animationDelta=0;
    DirectX::XMFLOAT3 interiorPoint{};
    size_t interiorCell=0;
    std::vector<std::string> clips;
};
OfflineEvidence checkOfflineAssets(const std::vector<ResolvedCell>& cells, RealSkeletalMeshResolver& skeletal) {
    OfflineEvidence result;
    for(size_t ci=0;ci<cells.size();++ci) {
        const auto& cell=cells[ci];const auto& floor=cell.floorMesh;
        for(size_t i=0;i+2<floor.triangleVertexIndices.size()&&result.floorChecks<128;i+=3) {
            const auto& a=floor.positions.at(floor.triangleVertexIndices[i]);const auto& b=floor.positions.at(floor.triangleVertexIndices[i+1]);const auto& c=floor.positions.at(floor.triangleVertexIndices[i+2]);
            DirectX::XMFLOAT3 point{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
            float area=std::abs((b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x));
            if(area<.05f)continue;
            auto hit=queryFloorHeight2DFullScan(floor,point,point.y);
            if(!hit||std::abs(hit->y-point.y)>.002f)throw std::runtime_error("Real floor height check failed");
            ++result.floorChecks;
            if(ci>0&&result.interiorCell==0&&area>2) {result.interiorCell=ci;result.interiorPoint=point;}
        }
        const auto& mesh=cell.collisionMesh;
        for(size_t i=0;i+2<mesh.indices.size()&&result.wallChecks<128;i+=3) {
            const auto& a=mesh.positions.at(mesh.indices[i]);const auto& b=mesh.positions.at(mesh.indices[i+1]);const auto& c=mesh.positions.at(mesh.indices[i+2]);
            auto ab=DirectX::XMVectorSet(b.x-a.x,b.y-a.y,b.z-a.z,0),ac=DirectX::XMVectorSet(c.x-a.x,c.y-a.y,c.z-a.z,0);
            auto n=DirectX::XMVector3Cross(ab,ac);
            if(DirectX::XMVectorGetX(DirectX::XMVector3Length(n))<.01f)continue;
            n=DirectX::XMVector3Normalize(n);DirectX::XMFLOAT3 normal;DirectX::XMStoreFloat3(&normal,n);
            if(std::abs(normal.y)>.15f)continue;
            DirectX::XMFLOAT3 point{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
            DirectX::XMFLOAT3 from{point.x+normal.x*.2f,point.y,point.z+normal.z*.2f},to{point.x-normal.x*.2f,point.y,point.z-normal.z*.2f};
            DirectX::XMFLOAT3 clear{point.x+normal.x*.3f,point.y,point.z+normal.z*.3f};
            if(segmentBlockedByCollisionMesh(mesh,clear,from))continue;
            if(!segmentBlockedByCollisionMesh(mesh,from,to))throw std::runtime_error("Real wall crossing check failed");
            ++result.wallChecks;
        }
    }
    for(size_t ci=1;ci<cells.size();++ci)if(cells[ci].bounds&&!cells[ci].floorMesh.positions.empty()) {
        const auto& b=*cells[ci].bounds;
        DirectX::XMFLOAT3 point{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
        auto hit=queryFloorHeight2DFullScan(cells[ci].floorMesh,point,point.y);
        if(hit){point.y=hit->y;result.interiorCell=ci;result.interiorPoint=point;break;}
    }
    if(result.floorChecks<10||result.wallChecks<10||result.interiorCell==0)throw std::runtime_error("Insufficient real floor/wall evidence");
    auto data=skeletal.resolveSelfAnimationData(soe::MessageHash::compute("object/creature/player/shared_human_male.iff"));
    if(!data||data->meshParts.empty()||data->stateTable.states.empty())throw std::runtime_error("Real character animation assets failed to resolve");
    for(bool moving:{false,true}) {
        creatureanim::CreatureAnimationContext context;context.isMoving=moving;
        auto state=creatureanim::stateNameFor(context);
        auto selection=creatureanim::toSelectionContext(context,data->gender);
        auto path=selfClipPathForState(data->stateTable,state,selection,moving);
        auto clip=skeletal.resolveAnimationClip(path);
        if(!clip)throw std::runtime_error("Missing real idle/walk clip: "+path);
        result.clips.push_back(path);
        auto clipBinding=animation::bindClipBoneIndices(data->skeleton,*clip);
        auto poseA=animation::computeWorldBoneTransforms(data->skeleton,animation::sampleLocalBoneTransforms(data->skeleton,&*clip,clipBinding,0));
        auto poseB=animation::computeWorldBoneTransforms(data->skeleton,animation::sampleLocalBoneTransforms(data->skeleton,&*clip,clipBinding,15));
        float maxDelta=0;
        for(const auto& part:data->meshParts) {
            auto meshBinding=animation::bindMeshBoneIndices(data->skeleton,part.boneNames);
            for(const auto& mesh:part.submeshes) {
                std::vector<assets::Float3> a,b,n;
                animation::skinSubmeshVertices(mesh,part.vertexWeights,data->skeleton,meshBinding,part.boneNames,poseA,a,n);
                animation::skinSubmeshVertices(mesh,part.vertexWeights,data->skeleton,meshBinding,part.boneNames,poseB,b,n);
                if(a.size()!=b.size()||a.size()!=mesh.positions.size())throw std::runtime_error("Skinned vertex count mismatch");
                for(size_t i=0;i<a.size();++i) {
                    if(!std::isfinite(b[i].x)||!std::isfinite(b[i].y)||!std::isfinite(b[i].z)||std::abs(b[i].x)>20||std::abs(b[i].y)>20||std::abs(b[i].z)>20)throw std::runtime_error("Invalid real animated vertex");
                    float dx=a[i].x-b[i].x,dy=a[i].y-b[i].y,dz=a[i].z-b[i].z;maxDelta=std::max(maxDelta,std::sqrt(dx*dx+dy*dy+dz*dz));
                }
                result.animatedVertices+=a.size();
            }
        }
        if(maxDelta<.00001f)throw std::runtime_error("Real animation did not move vertices");
        result.animationDelta=std::max(result.animationDelta,maxDelta);
    }
    std::cout<<"[OFFLINE TEST] "<<result.floorChecks<<" floor heights, "<<result.wallChecks<<" wall crossings, "<<result.animatedVertices<<" animated samples verified\n";
    return result;
}

// Shared authored graph: offline fixture and native live self use the same
// parsers, priorities, action aliases, raw pose math and controller lifecycle.
static void configureAuthoredPlayback(std::optional<animation::ActionPlayback>& playback,
    std::vector<std::string>& clipPaths,std::vector<std::string>& actionNames,
    std::vector<std::string>& unsupportedActions,const SelfAnimationData& character,
    RealMeshResolver& meshResolver,RealSkeletalMeshResolver& skeletalMeshResolver,bool ambientEligible) {
        auto state=std::find_if(character.stateTable.states.begin(),character.stateTable.states.end(),
                               [](const auto& s){return s.name=="loop_standing";});
        if(state==character.stateTable.states.end())throw std::runtime_error("Missing normal standing graph");
        clipPaths=assets::selectSpeedAnimationClips(state->root,{character.gender,"neutral"});
        std::vector<assets::AnimationClipData> clips;
        for(const auto& path:clipPaths) {
            auto clip=skeletalMeshResolver.resolveAnimationClip(path);
            if(!clip)throw std::runtime_error("Missing speed graph child: "+path);
            clips.push_back(std::move(*clip));
            std::cout<<"[AUTHORED ANIMATION] authored speed child "<<path<<"\n";
        }
        const auto priorityBytes=meshResolver.extract("animation/priority_map.iff");
        const auto hierarchyBytes=meshResolver.extract(character.stateTable.hierarchyFilename);
        if(!priorityBytes||!hierarchyBytes)throw std::runtime_error("Missing authored action priorities/hierarchy");
        const auto priorityMap=assets::AnimationPriorityMap::parse(*priorityBytes);
        const auto actionMap=assets::AnimationActionMap::parse(*hierarchyBytes);
        const assets::AnimationSelectionContext context{character.gender,"neutral"};
        const auto speedNodes=assets::selectSpeedAnimationNodes(state->root,context);
        auto prioritiesFor=[&](const assets::AnimationNode& node) {
            const auto* leaf=assets::selectAnimationClipNode(node,context);
            if(!leaf)throw std::runtime_error("Composite priority group unsupported");
            return animation::AnimationPriorities{priorityMap.groupPriorities.at(leaf->transformPriorityGroupName),
                                                  priorityMap.groupPriorities.at(leaf->punfParameterName)};
        };
        std::vector<animation::AnimationPriorities> basePriorities;
        for(const auto* node:speedNodes)basePriorities.push_back(prioritiesFor(*node));
        playback.emplace(character.skeleton,std::move(clips),std::move(basePriorities));
        const auto* idle=assets::resolveAnimationNode(*speedNodes.at(0),context);
        if(!idle||idle->kind!=assets::AnimationNodeKind::Variant)throw std::runtime_error("Expected authored idle AGAT");
        for(const auto& trigger:idle->actionTriggers) {
            const auto& mapping=actionMap.actions.at(trigger.name);
            // These fourteen recorded idle actions use the ordinary action
            // route; opaque ASH metadata is preserved by the parser. Additive
            // and grouped actions are deliberately not routed by this path.
            if(mapping.opaqueMetadata!=std::array<uint8_t,2>{0,0})throw std::runtime_error("Unsupported idle action metadata");
            const auto actionState=std::find_if(character.stateTable.states.begin(),character.stateTable.states.end(),
                [&](const auto& candidate){return candidate.name==mapping.animationStateName;});
            if(actionState==character.stateTable.states.end())throw std::runtime_error("Missing authored action LAT state");
            const auto* leaf=assets::selectAnimationClipNode(actionState->root,context);
            if(!leaf)throw std::runtime_error("Unsupported composite idle action");
            auto clip=skeletalMeshResolver.resolveAnimationClip(leaf->clipPath);
            if(!clip) {
                unsupportedActions.push_back(trigger.name);
                std::cerr<<"[AUTHORED ACTION] Unsupported clip format for "<<trigger.name<<"; request remains unplayed\n";
                continue;
            }
            playback->registerAction(trigger.name,std::move(*clip),prioritiesFor(*leaf));
            actionNames.push_back(trigger.name);
            std::cout<<"[AUTHORED ACTION] "<<trigger.name<<" -> "<<mapping.animationStateName<<" -> "<<leaf->clipPath<<"\n";
        }
        // Explicit offline policy: stationary actions enabled with a local
        // deterministic RNG; this is not the original game's random history.
        playback->setAmbient(*idle,[rng=std::minstd_rand(1977)]() mutable {return uint32_t(rng());});
        playback->setAmbientEligible(ambientEligible);

}

void runVisualizerImpl(soe::SoeSession* zoneSession, soe::MessageDispatcher* dispatcher, bool& failed,
                    const worldmodel::ObjectStore* objectStore, const std::string& clientPath,
                    const std::string& terrainName, int autoRestPoseTestSecondsPerPhase,
                    int autoRestPoseVariantSweepSecondsPerPhase,
                    int autoRestPoseBindAxisSweepSecondsPerPhase,
                    int autoRestPoseBindOnlySecondsPerPhase, const OfflineWorldOptions* offline, bool nativeClient=false,clientcommon::ChatLog* chatLog=nullptr,clientcommon::CommandSubmission* commandSubmission=nullptr,clientcommon::TellSubmission* tellSubmission=nullptr) {
    // kObjControllerMessageHash forwarding is no longer registered locally
    // here - main() now owns it permanently (see its 2026-07-18 fix comment).
    // Begin receiving/ACKing and sending net-status before expensive local
    // asset and graphics initialization. Handlers are already registered.
    // This RAII owner joins before returning or unwinding an asset failure.
    std::unique_ptr<NetworkThread> networkThread;
    if(zoneSession&&dispatcher)networkThread=std::make_unique<NetworkThread>(*zoneSession,*dispatcher,nativeClient?(std::filesystem::path(clientPath)/"modernization/live-client/reliability/reports").string():std::string{});
    renderer::Window window(1000, 740, offline ? L"SWG Offline World — Loading local assets…" : L"SWG Client Visualizer");
    renderer::VulkanRenderer gfx(window.handle(), window.width(), window.height());
    window.enableTextEntry(nativeClient);window.enableTextSubmission(nativeClient&&commandSubmission);clientcommon::ChatSnapshot chatSnapshot,chatPresentation,chatSelected,chatSelectedPresentation;uint64_t lastSelectedRawRevision=UINT64_MAX,resolveEpoch=0;
    std::optional<dummyclient::CommandCatalog> commandCatalog;
    clientcommon::ActionToolbar actionToolbar;
    std::optional<dummyclient::LocalizedChat> localizedChat;clientcommon::ChatResolveSchedule localizedSchedule;bool unresolvedLocalized=false;uint64_t lastLocalizedResolveMs=0,lastLocalizedRevision=0,lastPresentedRawRevision=0;size_t lastLocalizedResolved=0,lastLocalizedDeferred=0;
    clientcommon::TargetSelection targetSelection;
    std::optional<dummyclient::TargetHud> targetHud;

    bool lastChatFocus=false,chatFirstFrame=true;size_t lastDraftUnits=0,chatInputEvents=0,chatJournalEvents=0;
    std::string lastNoticeRecord;
    std::ofstream chatJournal;const auto chatStart=std::chrono::steady_clock::now();
    if(nativeClient){auto folder=std::filesystem::path(clientPath)/"modernization/live-client/chat/reports";std::filesystem::create_directories(folder);
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        chatJournal.open(folder/("chat-session-"+std::to_string(stamp)+".jsonl"));}
    auto chatElapsed=[&](){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-chatStart).count();};
    uint64_t targetLoggedId=UINT64_MAX;bool targetLoggedResolved=false;size_t targetLogEvents=0;
    std::ofstream targetJournal,statusJournal;
    std::string lastStatusRecord;size_t statusLogEvents=0;
    if(nativeClient) {
        auto folder=std::filesystem::path(clientPath)/"modernization/live-client/targeting/reports";
        std::filesystem::create_directories(folder);
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        targetJournal.open(folder/("target-session-"+std::to_string(stamp)+".jsonl"));
        targetJournal<<std::setprecision(9);
        auto statusFolder=std::filesystem::path(clientPath)/"modernization/live-client/hud/status/reports";
        std::filesystem::create_directories(statusFolder);
        statusJournal.open(statusFolder/("status-session-"+std::to_string(stamp)+".jsonl"));
        statusJournal<<std::setprecision(9);
    }
    const bool smoke=offline&&!offline->smokeOutput.empty();
    const bool actionSmoke=smoke&&offline->actionSmoke;
    const bool materialSmoke=smoke&&offline->materialSmoke;
    const bool cameraSmoke=smoke&&offline->cameraSmoke;
    const bool floorSmoke=smoke&&offline->floorSmoke;
    const bool cellSmoke=smoke&&offline->cellSmoke;
    const bool frameSmoke=cellSmoke&&offline->frameSmoke;
    size_t frameModelChecks=0;float frameMaxModelError=0;
    const bool serverCellTracking=nativeClient||cellSmoke;
    size_t cellEnteredFrames=0,cellExitedFrames=0,cellBlockedFrames=0,cellEnterCommits=0,cellExitCommits=0;
    float cellMaxWireError=0;
    DirectX::XMFLOAT3 cellDoorCenter{};
    size_t floorTerrainFrames=0,floorMeshFrames=0,floorBuriedFrames=0,floorRampCheckFrames=0;
    float floorMaxStationaryError=0,floorMaxStep=0,floorMinLocalZ=1000,floorMaxRampError=0;
    const std::vector<std::string> materialFixturePaths={"object/creature/player/shared_human_female.iff", "object/mobile/shared_gaping_spider.iff"};
    size_t materialFixtureTexturedDraws=0;
    bool smokeComplete=false;
    auto startupPump=[&] {
        bool open=window.pumpMessages();
        if(!open&&smoke)throw std::runtime_error("Offline smoke interrupted before completion");
        return open;
    };

    // Real-mesh resolution (2026-07-18) - opens real client archives once
    // at startup; a resolution failure for any given object just falls back
    // to the existing wireframe placeholder box, never blocks rendering.
    // Its CPU-side resolveMeshDataOnly() now runs on the asset worker
    // thread (Phase 14, constructed below) rather than inline in the draw
    // loop - meshResolver itself just needs to outlive that thread.
    RealMeshResolver meshResolver(clientPath);
    if(offline||nativeClient)targetHud.emplace(gfx,[&](const std::string& path){return meshResolver.extract(path);},nativeClient,nativeClient);
    if(nativeClient&&commandSubmission){
        auto chatAssets=[&](const std::string& path){return meshResolver.extract(path);};
        commandCatalog=dummyclient::CommandCatalog::load(chatAssets);localizedChat.emplace(chatAssets);
        constexpr std::array<const char*,4> firstActions={"attack","peace","kneel","stand"};
        for(size_t i=0;i<firstActions.size();++i){
            const auto found=commandCatalog->definitions().find(firstActions[i]);
            const bool available=found!=commandCatalog->definitions().end()&&!found->second.disabled&&
                (!found->second.clientOwned||i>=2);
            auto label=found!=commandCatalog->definitions().end()?found->second.label:std::u16string(firstActions[i],firstActions[i]+std::strlen(firstActions[i]));
            actionToolbar.configure(i,firstActions[i],std::move(label),available,i==0);
        }
    }
    if(!startupPump())return;
    MeshHandleCache meshCache;

    // Real skeletal-mesh resolution (Phase 15) - separate resolver/cache
    // from the static-mesh pair above, since RealMeshResolver's own
    // candidate list explicitly excludes creature/mobile templates (see its
    // own comment). Construction opens its own archive handles rather than
    // sharing meshResolver's, matching RealMeshResolver's own "each
    // resolver owns its archives" precedent (findArchiveNamed() exists for
    // the one case - TerrainResolver - that specifically needs to share).
    RealSkeletalMeshResolver skeletalMeshResolver(clientPath);
    if(!startupPump())return;
    SkeletalMeshHandleCache skeletalMeshCache;

    // Real building resolution (Phase 16) - unlike the skeletal-mesh pair
    // above, this DOES share meshResolver's already-open archives
    // (findArchiveNamed(), same as TerrainResolver's own precedent just
    // below) rather than opening a second copy, since buildings live in
    // the exact same archive set ordinary static objects do and are
    // already covered by the same bundled candidate list.
    RealBuildingResolver buildingResolver(meshResolver);
    BuildingHandleCache buildingCache;

    // Real terrain grading (structure footprint flattening around a placed
    // building) - tracks which real objectIds have already had a grading
    // request sent (so it's only ever sent once per instance, not every
    // frame) and which ones are still actually present each frame (a
    // building removed from the world needs its grading un-registered -
    // see the per-frame diff right after the object-store scan below,
    // mirroring AssetWorkerThread's own terrain-chunk eviction diffing).
    std::unordered_set<uint64_t> gradedBuildingIds;

    // Real terrain (Phase 8 of the terrain plan) - resolved once at
    // startup from the real terrainName CmdStartScene reported at zone-in,
    // reusing meshResolver's already-open data_other_00.tre rather than
    // opening a second copy of the same archive. A null terrainSource
    // (empty name, missing archive, parse failure) just means the asset
    // worker thread's own TerrainChunkManager never gets created - no
    // ground surface renders at all in that case (the flat placeholder
    // reference grid was removed 2026-07-20, once real terrain covered
    // every case it used to fall back for).
    TerrainResolver terrainResolver(meshResolver);
    std::shared_ptr<terrain::TerrainSource> terrainSource = terrainResolver.resolve(terrainName);
    std::unordered_map<terrain::ChunkCoord, renderer::TerrainChunkHandle, terrain::ChunkCoordHash>
        terrainChunkHandles;
    std::unordered_map<terrain::ChunkCoord,std::shared_ptr<const renderer::CameraCollisionMesh>,terrain::ChunkCoordHash> terrainCameraMeshes;

    std::vector<worldmodel::ObjectVariant> offlineObjects;
    DirectX::XMFLOAT3 offlineSpawn{},offlineBuildingOrigin{};
    worldmodel::SpatialTransform offlineBuildingFrame{{0,0,0},{0,0,0,1}};
    std::vector<ResolvedCell> offlineCells;
    OfflineEvidence offlineEvidence;
    bool offlineTitleReady=false;
    int smokePhase=-1,smokeFrames=0;
    auto smokeStart=std::chrono::steady_clock::now();
    DirectX::XMFLOAT3 smokeWalkStart{};
    float smokeWalkYawRadians=0;
    float smokeWalkDistance=0;
    size_t smokeBlockedFrames=0,maxTerrainChunks=0,smokeWalkMovementFrames=0;
    float offlineWalkSpeed=4.5f;
    float offlineWalkFps=30,offlineWalkDurationFrames=0;
    std::optional<animation::ActionPlayback> offlinePlayback;
    std::vector<std::string> offlineActionNames,offlineUnsupportedActions;
    size_t offlineActionIndex=0;
    bool offlineAmbientEligible=!smoke;
    struct ActionTrace {int frame,mode,locomotionMode;float weight,actionFrame,idleFrame,replacementWeight;bool active,routed;std::string name;};
    std::vector<ActionTrace> actionTraces;
    std::vector<std::string> offlineSpeedClipPaths;
    bool offlineRun=false;
    std::optional<animation::ActionPlayback> livePlayback;
    std::vector<std::string> liveClipPaths,liveActionNames,liveUnsupportedActions;
    bool liveRun=false,liveAmbientEligible=true,liveStanding=true;
    size_t liveActionIndex=0;
    float liveWalkSpeed=1.55f,liveRunSpeed=5.13f;
    float smokeWalkStartFrame=0;
    struct TransitionSample {float elapsed,idleFrame,movingFrame,weight;std::vector<DirectX::XMFLOAT4X4> localMatrices;};
    std::vector<TransitionSample> smokeTransitionSamples;
    DirectX::XMFLOAT3 smokeExpectedWalkDelta{};
    constexpr uint64_t offlineBuildingId=200;
    if(offline) {
        if(!terrainSource) throw std::runtime_error("Offline world requires the real terrain source");
        const std::string buildingPath="object/building/player/shared_player_guildhall_generic_style_01.iff";
        auto buildingCrc=soe::MessageHash::compute(buildingPath);
        offlineCells=buildingResolver.resolveMeshDataOnly(buildingCrc);
        if(offlineCells.size()<2) throw std::runtime_error("Offline building has no resolved interior");
        if(smoke) {std::filesystem::create_directories(offline->smokeOutput);offlineEvidence=checkOfflineAssets(offlineCells,skeletalMeshResolver);}
        auto character=skeletalMeshResolver.resolveSelfAnimationData(soe::MessageHash::compute("object/creature/player/shared_human_male.iff"));
        if(!character)throw std::runtime_error("Missing offline character speed graph");
        configureAuthoredPlayback(offlinePlayback,offlineSpeedClipPaths,offlineActionNames,
            offlineUnsupportedActions,*character,meshResolver,skeletalMeshResolver,offlineAmbientEligible);
        offlineWalkSpeed=offlinePlayback->walkingSpeed();
        offlineWalkFps=offlinePlayback->clip(1).framesPerSecond;
        offlineWalkDurationFrames=offlinePlayback->clip(1).durationFrames;
        float x=-2736,z=2136,bestRange=1e30f;
        // These are offline fixture placements, not an invented live city.
        // Find an asset-derived flat area spanning both buildings and the approach.
        bool found=false;
        for(int ring=0;ring<=10&&!found;++ring)for(int dx=-ring;dx<=ring&&!found;++dx)for(int dz=-ring;dz<=ring&&!found;++dz) {
            if(std::max(std::abs(dx),std::abs(dz))!=ring)continue;
            float cx=-2736+dx*256,cz=2136+dz*256,lo=1e30f,hi=-1e30f;
            for(float ox:{-32.f,0.f,32.f,88.f,120.f,152.f})for(float oz:{-32.f,0.f,32.f,70.f}) {
                float h=terrainSource->querySurfaceHeight(cx+ox,cz+oz);lo=std::min(lo,h);hi=std::max(hi,h);
            }
            if(hi-lo<bestRange){bestRange=hi-lo;x=cx;z=cz;}
            found=bestRange<.8f;
        }
        if(bestRange>3)throw std::runtime_error("No suitable offline placement area found");
        offlineBuildingOrigin={x,terrainSource->querySurfaceHeight(x,z),z};
        std::cout<<"[OFFLINE] placement "<<x<<","<<offlineBuildingOrigin.y<<","<<z<<" sampled relief="<<bestRange<<"m\n";
        offlineBuildingFrame={{x,offlineBuildingOrigin.y,z},worldmodel::headingRotation(frameSmoke?.7312345f:0)};
        for(int i=0;i<2;++i) {
            worldmodel::TangibleObject building;
            building.objectId=offlineBuildingId+i;building.typeTag=worldmodel::ObjectTypeTag::Static;
            building.objectCrc=buildingCrc;building.x=x+120*i;building.y=terrainSource->querySurfaceHeight(building.x,z);building.z=z;
            building.transformMessagesSeen=1;
            if(frameSmoke){auto q=worldmodel::headingRotation(i==0?.7312345f:-1.432137f);building.fullRotationCurrent=true;building.quatY=q.y;building.quatW=q.w;}
            offlineObjects.emplace_back(building);
        }
        offlineSpawn={x,terrainSource->querySurfaceHeight(x,z+65),z+65};
        if(cameraSmoke&&offlineCells.front().bounds) {
            const auto& bounds=*offlineCells.front().bounds;
            offlineSpawn.x=x+(bounds.min.x+bounds.max.x)*.5f;
            offlineSpawn.z=z+std::max(65.f,bounds.max.z+4);
            offlineSpawn.y=terrainSource->querySurfaceHeight(offlineSpawn.x,offlineSpawn.z);
        }
        if(floorSmoke)offlineSpawn={x,terrainSource->querySurfaceHeight(x,z+36.5f),z+36.5f};
        if(cellSmoke) {
            auto door=std::find_if(offlineCells[0].portals.begin(),offlineCells[0].portals.end(),[](auto r){return r.adjacentCellIndex==1;});
            if(door==offlineCells[0].portals.end())throw std::runtime_error("Cell smoke: no exterior entrance");
            const auto& shape=offlineCells[0].portalShapes.at(door->portalShapeIndex);
            for(auto v:shape.vertices){cellDoorCenter.x+=v.x/shape.vertices.size();cellDoorCenter.y+=v.y/shape.vertices.size();cellDoorCenter.z+=v.z/shape.vertices.size();}
            // The retained guildhall door is on +Z. Fail if a different asset
            // replaces it, rather than silently replaying a wrong path.
            if(std::abs(cellDoorCenter.x)>.002f||cellDoorCenter.z<15||cellDoorCenter.z>20)throw std::runtime_error("Cell smoke: unexpected entry geometry");
            auto floor=queryFloorHeight2DFullScan(offlineCells[0].floorMesh,{cellDoorCenter.x,.5f,cellDoorCenter.z+2},.5f);
            if(!floor)throw std::runtime_error("Cell smoke: no exterior entry floor");
            auto supported=offlineBuildingFrame.toWorld({cellDoorCenter.x,floor->y,cellDoorCenter.z+2});
            offlineSpawn={supported.x,supported.y,supported.z};
            for(size_t i=1;i<offlineCells.size();++i) {
                worldmodel::CellObject cell;cell.objectId=1000+i;cell.typeTag=worldmodel::ObjectTypeTag::Cell;
                cell.containerId=offlineBuildingId;cell.containmentMessagesSeen=1;cell.base3.emplace();cell.base3->cellNumber=int32_t(i);
                offlineObjects.emplace_back(cell);
            }
            worldmodel::TangibleObject blocker;blocker.objectId=500;blocker.typeTag=worldmodel::ObjectTypeTag::Tangible;
            blocker.objectCrc=soe::MessageHash::compute("object/tangible/terminal/shared_terminal_bank.iff");
            blocker.x=x+90;blocker.y=offlineSpawn.y;blocker.z=z+90;blocker.transformMessagesSeen=1;offlineObjects.emplace_back(blocker);
        }
        worldmodel::CreatureObject self;
        self.objectId=100;self.isSelf=true;self.typeTag=worldmodel::ObjectTypeTag::Creature;
        self.objectCrc=soe::MessageHash::compute("object/creature/player/shared_human_male.iff");
        self.x=offlineSpawn.x;self.y=offlineSpawn.y;self.z=offlineSpawn.z;self.direction=50;self.transformMessagesSeen=1;
        offlineObjects.emplace_back(self);
        if (materialSmoke) {
            auto fixtures=dummyclient::resolveCreatureMaterials(clientPath, materialFixturePaths);
            dummyclient::writeCreatureMaterialAudit(fixtures, (std::filesystem::path(offline->smokeOutput)/"asset-audit.json").string());
            for (size_t i=0;i<materialFixturePaths.size();++i) {
                worldmodel::CreatureObject creature;
                creature.objectId=300+i;creature.typeTag=worldmodel::ObjectTypeTag::Creature;
                creature.objectCrc=soe::MessageHash::compute(materialFixturePaths[i]);
                creature.x=offlineSpawn.x+(i==0?-3.f:3.f);creature.z=offlineSpawn.z-2;
                creature.y=terrainSource->querySurfaceHeight(creature.x,creature.z);
                creature.transformMessagesSeen=1;offlineObjects.emplace_back(creature);
            }
        }
        std::cout<<"[OFFLINE] terrain + two locally placed guildhalls + human; no networking\n";
    }
    auto forEachObject=[&](auto&& fn) {
        if(offline) {for(const auto& obj:offlineObjects)std::visit(fn,obj);}
        else objectStore->forEach(fn);
    };

    // Phase 14 - real terrain-chunk generation (the actual measured
    // ~126ms/chunk cost, not just mesh triangulation) and real-mesh CPU
    // parsing both now happen on this dedicated thread instead of inline
    // in the render loop. Radius 2 -> a 5x5 chunk window (128m chunks - see
    // ProceduralTerrainSource::kChunkWidthInMeters - so ~640m across),
    // chosen for a reasonable initial-load pause (measured this session:
    // ~25 chunks x ~126ms/chunk in a Release build) rather than the plan's
    // own suggested default of 3 - unchanged from before, just relocated.
    DirectX::XMFLOAT3 initialStreamPosition=offlineSpawn;
    if(nativeClient) {
        std::unordered_map<uint64_t,worldmodel::WorldObject> initialSnapshot;
        forEachObject([&](const auto& obj){initialSnapshot.emplace(obj.objectId,obj);});
        for(const auto& [id,obj]:initialSnapshot)if(obj.isSelf&&obj.transformMessagesSeen>0) {
            if(auto position=resolveWorldPosition(initialSnapshot,obj))initialStreamPosition=*position;
        }
    }
    AssetWorkerThread assetWorker(meshResolver, skeletalMeshResolver, buildingResolver, terrainSource,
                                    /*radiusInChunks=*/2, initialStreamPosition.x, initialStreamPosition.z);
    constexpr int kMaxAssetUploadsPerFrame = 2; // tunable - see Phase 14 plan's own note

    // Third-person follow camera, locked to self (2026-07-18, replacing the
    // earlier free FlyCamera per user request now that self reliably
    // renders): position is fully re-derived from self's live WorldObject
    // position every frame rather than cached/seeded once, so it tracks self
    // even if self later moves. `lastKnownSelfPos` only matters before
    // self's first seeded position arrives (a brief window right at zone-in)
    // or in the defensive case self is somehow never found - initialized to
    // the origin, same fallback the old camera used before its own
    // self-centering fix.
    renderer::FollowCamera camera;
    renderer::CameraBoom cameraBoom;
    if(offline){camera.yaw=DirectX::XM_PI;camera.pitch=-.22f;}
    if(materialSmoke)camera.distance=12;
    // Phase 18 - a second, free noclip camera for full-building inspection
    // (see the 'I' toggle below): lets the user pull back and see a whole
    // building's real geometry at once instead of only the exterior shell
    // or their own current cell, without touching the normal gameplay-
    // accurate FollowCamera/current-cell-filter path used the rest of the
    // time.
    renderer::FlyCamera flyCamera;
    bool inspectionMode = false;
    bool inspectKeyWasDown = false; // edge-detect, same reasoning as leftMouseWasDown below
    DirectX::XMFLOAT3 lastKnownSelfPos{0.0f, 0.0f, 0.0f};
    float lastKnownSelfYawRadians = 0.0f; // for the minimap's facing indicator

    // Animation debug-key bundle (V/C/T/N/Z/R/X/L/F/G/H/U/M/B) and the
    // locked-camera/burst-screenshot pipeline (K/J/P) - see
    // AnimationDebugControls.h/ScreenshotCapture.h for what each owns; both
    // used to live inline here. autoTest drives both automatically when a
    // --rest-pose-* CLI flag was passed (see RestPoseAutoTest.h) - normal
    // interactive runs leave it disabled and it's a no-op.
    dummyclient::AnimationDebugControls animControls;
    dummyclient::ScreenshotCapture screenshotCapture;
    dummyclient::RestPoseAutoTest autoTest(autoRestPoseTestSecondsPerPhase,
                                            autoRestPoseVariantSweepSecondsPerPhase,
                                            autoRestPoseBindAxisSweepSecondsPerPhase,
                                            autoRestPoseBindOnlySecondsPerPhase);

    // Render from local prediction. The native client seeds from the resolved
    // server position and consumes fresh explicit authoritative updates once.
    struct RemotePresentationState {
        clientcommon::RemoteMotion motion;
        uint32_t revision=0,authoritativeGeneration=0;
    };
    std::unordered_map<uint64_t,RemotePresentationState> remotePresentation;
    uint64_t remotePresentationProbeId=0;
    size_t remotePresentedSamples=0;
    if(nativeClient)std::cout<<"[NETWORK] Remote movement smoothing enabled.\n";
    // Ordinary peer movement broadcasts do not overwrite local input every frame.
    DirectX::XMFLOAT3 predictedSelfPos{0.0f, 0.0f, 0.0f};
    float predictedSelfYawRadians = 0.0f;
    bool predictedSelfInitialized = false;
    uint64_t selfObjectId = 0;

    // Phase 21 (animation) - promoted out of the WASD-handling block's own
    // local `movementKeyHeld` (which resets every frame and isn't visible
    // outside that block) into small persistent locomotion state so the
    // draw loop's own animation state-selection (further down, same frame)
    // can read this frame's movement intent.
    bool selfIsMoving = false;

    // Phase 21 (animation) - self-only real animation state (v1 scope, see
    // SelfAnimationData's own comment on why other visible players/
    // creatures still render bind-pose only this phase). Resolved once,
    // synchronously, the first frame self's real objectCrc becomes known
    // (see the draw loop below). `selfDynamicMeshes` holds one
    // renderer::DynamicMeshHandle per real submesh across every resolved
    // body part (flattened, parallel to iterating `animData->meshParts`
    // then each part's own `submeshes`); `selfMeshBoneBindings` is the
    // matching per-body-part bone-name binding (see
    // animation::bindMeshBoneIndices). `selfClipCache` memoizes parsed real
    // `.ans` clips by their real archive path, resolved lazily the first
    // time a given clip is actually needed to animate self (avoids parsing
    // all 664 real states' worth of clips up front for the handful this
    // phase's small state map ever selects).
    std::optional<SelfAnimationData> selfAnimData;
    std::vector<std::vector<int>> selfMeshBoneBindings; // parallel to selfAnimData->meshParts
    std::vector<renderer::DynamicMeshHandle> selfDynamicMeshes;
    std::unordered_map<std::string, std::optional<assets::AnimationClipData>> selfClipCache;
    float selfAnimFrame = 0.0f;
    // Measured from the actual render-loop pose, for offline gait diagnostics.
    std::vector<std::array<float,3>> smokeKneeSamples;
    std::vector<std::array<float,7>> smokeHipSamples;
    bool selfAnimResolveAttempted = false;
    std::string selfActiveAnimationState;

    // Phase 17 Step 4 - self's currently-tracked containing cell (0 =
    // outdoors/world-relative). Real capture evidence changed the original
    // plan here: rather than deriving floor height from resolved cell mesh
    // geometry (no CPU-side bounding-box data survives past GPU upload
    // today, and would need new plumbing through AssetWorkerThread/
    // BuildingHandleCache to get it), the server ALREADY broadcasts self's
    // own real cell-relative Y via DataTransformWithParent/
    // UpdateTransformWithParentMessage (confirmed live this session, e.g.
    // idle-sync arriving mid-elevator-ride) - simpler, more accurate (it's
    // the server's own authoritative value, not an approximation), and
    // needs no new geometry code at all. So: while indoors, Y is taken
    // directly from the server's own reports (via resolveWorldPosition)
    // instead of a local terrain-height clamp; X/Z stay locally
    // WASD-predicted exactly as outdoors, since floors are flat in
    // practice and real per-frame terrain-style height sampling isn't
    // needed. A cell CHANGE (walking into a different room, riding the
    // elevator) snaps predictedSelfPos straight to the server-resolved
    // world position instead of continuing local prediction through it -
    // correct for a real teleport-like transition (an elevator ride is
    // visually discontinuous anyway), and avoids needing to reconcile
    // stale local X/Z against a coordinate frame that just changed
    // meaning.
    uint64_t predictedSelfParentId = 0;

    // Phase 20b (portal-based cell transitions) - which real cell self is
    // currently believed to be standing in, persisted across frames rather
    // than re-derived from scratch every frame via the old coarse AABB
    // test. Only ever CHANGES when self's own movement segment actually
    // crosses one of the current cell's real portal shapes into the
    // connected adjacent cell (see segmentCrossesPortal()'s own comment) -
    // a real, precise transition signal instead of an approximate
    // bounding-box re-test, which was the root cause of the live-caught
    // boundary flakiness (sinking, "hard edge" snaps, stairs never
    // registering) the AABB-only approach kept hitting right at cell
    // edges. Reset to nullopt (forcing a fresh one-time AABB-based guess)
    // whenever self wasn't inside ANY building footprint last frame, or
    // the building itself changed - see wasInsideBuildingFootprintLastFrame's
    // own comment.
    std::optional<size_t> persistentCellIndex;
    uint64_t persistentCellBuildingId = 0;
    bool wasInsideBuildingFootprintLastFrame = false;
    worldmodel::CellTraversal cellTraversal;
    uint32_t journalSelfSyncGeneration=0;

    // Phase 20d - which real triangle of the CURRENT cell's own .flr
    // navmesh self is standing on, persisted across frames for the
    // adjacency-restricted query (see queryFloorHeight2DAdjacent's own
    // comment - this is what actually fixes the real switchback-staircase
    // ambiguity). Keyed by building instance and cell index; reset on fresh entry or whenever
    // the resolved cell for floor-height purposes changes, forcing a fresh
    // full-mesh scan (see queryFloorHeight2DFullScan) to re-seed it.
    std::optional<size_t> persistentFloorTriangleIndex;
    std::optional<size_t> persistentFloorTriangleCellIndex;
    uint64_t persistentFloorTriangleBuildingId=0;

    // Phase 17 Step 5 - edge-detects a fresh left-click (down this frame,
    // wasn't down last frame) rather than firing once per frame the button
    // is held, since Window only exposes level-triggered isMouseButtonDown().
    bool leftMouseWasDown = false;

    // Conservative starting walk speed (world units/second - SWG world units
    // are ~meters, per this project's own established convention) - well
    // under any real character's run speed, tuned against live server
    // behavior rather than a source-confirmed constant (Core3's own
    // checkSpeedHackTests() validates reported speed against actual
    // distance/time with a ~5-10% tolerance, not an exact real value we'd
    // need to match precisely).
    const float walkSpeedMetersPerSecond = offline ? offlineWalkSpeed : 4.5f;
    auto movementSendReferenceTime = std::chrono::steady_clock::now();
    auto movementMillis=[&](auto now){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now-movementSendReferenceTime).count());};
    clientcommon::MovementReporter movementReporter(0);
    clientcommon::SelfCorrectionTracker selfCorrectionTracker;
    std::ofstream movementJournal;
    size_t movementJournalEvents=0;
    std::unordered_map<uint64_t,uint32_t> journalTransformSeen;
    if(nativeClient||cellSmoke) {
        auto folder=nativeClient?std::filesystem::path(clientPath)/"modernization/live-client/movement/reports":std::filesystem::path(offline->smokeOutput);
        std::filesystem::create_directories(folder);
        const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto path=folder/("session-"+std::to_string(stamp)+".jsonl");
        movementJournal.open(path);movementJournal<<std::setprecision(9);
        if(movementJournal)std::cout<<"NATIVE_MOVEMENT_JOURNAL "<<path.string()<<"\n";
    }
    auto journalMovement=[&](const char* event,uint64_t id,uint64_t parent,uint32_t counter,float x,float y,float z,float speed, std::optional<uint64_t> pollMs={},uint32_t wireStamp=0) {
        if(!movementJournal||movementJournalEvents>=10000||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(speed))return;
        ++movementJournalEvents;
        movementJournal<<"{\"event\":\""<<event<<"\",\"elapsed_ms\":"<<movementMillis(std::chrono::steady_clock::now())<<",\"object_id\":"<<id<<",\"parent_id\":"<<parent<<",\"counter\":"<<counter<<",\"x\":"<<x<<",\"y\":"<<y<<",\"z\":"<<z<<",\"speed\":"<<speed;
        if(pollMs)movementJournal<<",\"poll_elapsed_ms\":"<<*pollMs<<",\"wire_timestamp\":"<<wireStamp;
        movementJournal<<"}\n";movementJournal.flush();
    };
    std::ofstream frameJournal;size_t frameJournalEvents=0;uint64_t frameSceneNext=0;
    if(nativeClient||frameSmoke) {
        auto folder=nativeClient?std::filesystem::path(clientPath)/"modernization/live-client/exploration/frames/reports":std::filesystem::path(offline->smokeOutput);
        std::filesystem::create_directories(folder);
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto path=folder/("frame-session-"+std::to_string(stamp)+".jsonl");frameJournal.open(path);frameJournal<<std::setprecision(9);
        if(frameJournal)std::cout<<"NATIVE_FRAME_JOURNAL "<<path.string()<<"\n";
    }
    auto journalFrame=[&](const clientcommon::MovementReport& report,const clientcommon::MovementFrame& world,
        const std::optional<clientcommon::MovementParentFrame>& parent) {
        if(!frameJournal||frameJournalEvents>=4000)return;++frameJournalEvents;
        auto wire=report.frame;auto q=*wire.orientation;
        worldmodel::SpatialTransform basis{{0,0,0},{0,0,0,1}};
        if(parent)basis={{parent->x,parent->y,parent->z},*worldmodel::normalizedRotation(parent->rotation.value_or(worldmodel::headingRotation(parent->yaw)))};
        auto model=spatialModelMatrix(basis);DirectX::XMFLOAT3 rendered;
        auto local=assets::Float3{wire.x,wire.y,wire.z};
        DirectX::XMStoreFloat3(&rendered,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(local.x,local.y,local.z,1),DirectX::XMLoadFloat4x4(&model)));
        // Outdoor reports bypass the parent transform even if a stale basis was supplied.
        if(!wire.parentId)rendered={wire.x,wire.y,wire.z};
        auto worldQ=*worldmodel::normalizedRotation(world.orientation.value_or(worldmodel::headingRotation(world.yaw)));
        frameJournal<<"{\"event\":\"wire_frame\",\"elapsed_ms\":"<<movementMillis(std::chrono::steady_clock::now())<<",\"counter\":"<<report.counter
            <<",\"object_id\":"<<wire.objectId<<",\"parent_id\":"<<wire.parentId<<",\"world_feet\":["<<world.x<<","<<world.y<<","<<world.z
            <<"],\"wire_feet\":["<<wire.x<<","<<wire.y<<","<<wire.z<<"],\"wire_rotation\":["<<q.x<<","<<q.y<<","<<q.z<<","<<q.w
            <<"],\"world_rotation\":["<<worldQ.x<<","<<worldQ.y<<","<<worldQ.z<<","<<worldQ.w<<"],\"parent_origin\":["<<basis.origin.x<<","<<basis.origin.y<<","<<basis.origin.z
            <<"],\"parent_rotation\":["<<basis.rotation.x<<","<<basis.rotation.y<<","<<basis.rotation.z<<","<<basis.rotation.w
            <<"],\"model_world_feet\":["<<rendered.x<<","<<rendered.y<<","<<rendered.z<<"]}\n";frameJournal.flush();
    };
    std::ofstream floorJournal;size_t floorJournalEvents=0;uint64_t floorJournalNext=0;
    if(nativeClient||floorSmoke||cellSmoke) {
        auto folder=nativeClient?std::filesystem::path(clientPath)/"modernization/live-client/exploration/floor/reports":std::filesystem::path(offline->smokeOutput);
        std::filesystem::create_directories(folder);
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto path=folder/("floor-session-"+std::to_string(stamp)+".jsonl");
        floorJournal.open(path);floorJournal<<std::setprecision(9);
        if(floorJournal)std::cout<<"NATIVE_FLOOR_JOURNAL "<<path.string()<<"\n";
    }
    auto journalFloor=[&](const char* source,uint64_t owner,size_t buried) {
        auto elapsed=movementMillis(std::chrono::steady_clock::now());
        if(!floorJournal||floorJournalEvents>=6000||(!floorSmoke&&!cellSmoke&&elapsed<floorJournalNext))return;
        floorJournalNext=elapsed+100;++floorJournalEvents;
        floorJournal<<"{\"elapsed_ms\":"<<elapsed<<",\"frame\":"<<smokeFrames<<",\"source\":\""<<source
            <<"\",\"building_id\":"<<owner<<",\"buried_candidates\":"<<buried<<",\"parent_id\":"<<predictedSelfParentId
            <<",\"feet\":["<<predictedSelfPos.x<<","<<predictedSelfPos.y<<","<<predictedSelfPos.z<<"]";
        if(terrainSource)floorJournal<<",\"terrain_height\":"<<terrainSource->querySurfaceHeight(predictedSelfPos.x,predictedSelfPos.z);
        floorJournal<<"}\n";floorJournal.flush();
    };
    std::ofstream cellJournal;size_t cellJournalEvents=0;uint64_t unresolvedCellJournalNext=0;
    std::unordered_map<uint64_t,std::tuple<uint64_t,int32_t,uint32_t,bool,uint32_t>> journalCellsSeen;
    if(nativeClient||cellSmoke) {
        auto folder=nativeClient?std::filesystem::path(clientPath)/"modernization/live-client/exploration/cells/reports":std::filesystem::path(offline->smokeOutput);
        std::filesystem::create_directories(folder);
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto path=folder/("cell-session-"+std::to_string(stamp)+".jsonl");
        cellJournal.open(path);cellJournal<<std::setprecision(9);
        std::cout<<"NATIVE_CELL_JOURNAL "<<path.string()<<"\n";
    }
    auto journalCell=[&](const worldmodel::CellCrossing& cross,uint64_t parent,const char* event) {
        if(!cellJournal||cellJournalEvents>=4000)return;
        if(std::string_view(event)=="unresolved") {
            auto elapsed=movementMillis(std::chrono::steady_clock::now());
            if(elapsed<unresolvedCellJournalNext)return;unresolvedCellJournalNext=elapsed+1000;
        }
        ++cellJournalEvents;
        cellJournal<<"{\"event\":\""<<event<<"\",\"elapsed_ms\":"<<movementMillis(std::chrono::steady_clock::now())
            <<",\"building_id\":"<<cross.buildingId<<",\"from_cell\":"<<cross.sourceCell<<",\"to_cell\":"<<cross.destinationCell
            <<",\"shape\":"<<cross.shape<<",\"fraction\":"<<cross.fraction<<",\"parent_id\":"<<parent
            <<",\"world_feet\":["<<predictedSelfPos.x<<","<<predictedSelfPos.y<<","<<predictedSelfPos.z<<"]}\n";cellJournal.flush();
    };
    bool cameraProbeActive=false;int cameraProbePhase=-1;
    renderer::FollowCamera savedProbeCamera;
    std::chrono::steady_clock::time_point cameraProbeStart;
    size_t cameraBuildingFrames=0,cameraTerrainFrames=0,cameraOverlapFrames=0;
    std::ofstream cameraJournal;size_t cameraJournalEvents=0;uint64_t cameraJournalNext=0;
    if(nativeClient||smoke) {
        auto folder=nativeClient?std::filesystem::path(clientPath)/"modernization/live-client/exploration/reports":std::filesystem::path(offline->smokeOutput);
        std::filesystem::create_directories(folder);
        auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto path=folder/("camera-session-"+std::to_string(stamp)+".jsonl");
        cameraJournal.open(path);cameraJournal<<std::setprecision(9);
        if(cameraJournal)std::cout<<"NATIVE_CAMERA_JOURNAL "<<path.string()<<"\n";
    }
    auto journalCamera=[&](float requested,float safe,float resolved,float radius,const char* kind,bool overlap,uint32_t tests,size_t terrainCount) {
        auto elapsed=movementMillis(std::chrono::steady_clock::now());
        if(!cameraJournal||cameraJournalEvents>=6000||elapsed<cameraJournalNext)return;
        cameraJournalNext=elapsed+100;++cameraJournalEvents;
        cameraJournal<<"{\"elapsed_ms\":"<<elapsed<<",\"requested_distance\":"<<requested<<",\"safe_distance\":"<<safe
            <<",\"resolved_distance\":"<<resolved<<",\"radius\":"<<radius<<",\"hit_kind\":\""<<kind<<"\",\"initial_overlap\":"<<(overlap?"true":"false")
            <<",\"triangle_tests\":"<<tests<<",\"terrain_chunks\":"<<terrainCount
            <<",\"probe_phase\":"<<cameraProbePhase<<",\"target\":["<<predictedSelfPos.x<<","<<predictedSelfPos.y+camera.targetHeightOffset<<","<<predictedSelfPos.z
            <<"],\"eye\":["<<camera.position.x<<","<<camera.position.y<<","<<camera.position.z<<"]}\n";
        cameraJournal.flush();
    };
    bool movementTestActive=false;
    float movementTestYaw=0;
    std::chrono::steady_clock::time_point movementTestStart;

    // Label textures (object name -> rasterized quad), created lazily on
    // first sight of each distinct name and reused every frame after -
    // avoids re-rasterizing the same string through GDI every single frame.
    std::unordered_map<std::string, LabelTexture> labelCache;

    std::cout << "\n[VISUALIZER] window open (1920x1080) - third-person camera locked to self, "
                 "hold the right mouse button to orbit, scroll wheel to zoom. Press 'I' to toggle "
                 "full-building inspection mode (free noclip camera, every cell of a building "
                 "visible at once). Press 'B' to toggle forcing self's animated skinning to the "
                 "real bind pose only, 'V' to cycle the animated-rotation composition variant, "
                 "'C' to cycle a real axis sign/swap correction, 'T' to toggle animated "
                 "translation, 'N' to cycle the real QCHN bit-layout hypothesis, 'M' to cycle "
                 "isolating a single real bone's animation, 'Z' to skip Z-negation for arm-chain "
                 "bones, 'R' to skip Z-negation for the root bone, 'L' to cycle a forced "
                 "dropped-axis override for leg-chain bones, 'F' to cycle the same override for "
                 "the finger+elbow/wrist chain, 'G' to cycle a forced composition-variant override "
                 "for that same chain, or 'H' to cycle a forced axis-fix-variant override for that "
                 "same chain (Phase 21 diagnostics - widened 2026-07-23 to also cover "
                 "forearm/ulna/wrist since they share the same artifact). Press 'K' to lock the "
                 "camera to a fixed angle relative to self's own facing (0/90/180/270deg), 'J' to "
                 "cycle which of those 4 angles is active, and 'P' to start/stop a burst PNG "
                 "screenshot capture (~6/sec) into diagnostic_screenshots/<angle>deg/ - added for "
                 "live walk-cycle visual comparison work. Close the window to exit.\n";

    using Clock = std::chrono::steady_clock;
    auto lastFrameTime = Clock::now();
    const auto targetFrameTime = std::chrono::milliseconds(1000 / 30); // 30fps cap


    while (!failed && !autoTest.complete() && !(nativeClient&&clientcommon::clientCancellationRequested())) {
        if (!window.pumpMessages()) {
            break;
        }
        if(nativeClient&&chatLog)if(auto selection=window.takeChatTabSelection())if(chatLog->selectTab(*selection)){
            if(chatJournal&&chatJournalEvents<512){++chatJournalEvents;const auto counts=chatLog->tabLineCounts();chatJournal<<"{\"event\":\"tab_selected\",\"elapsed_ms\":"<<chatElapsed()<<",\"tab\":"<<*selection<<",\"counts\":["<<counts[0]<<","<<counts[1]<<","<<counts[2]<<","<<counts[3]<<"]}\n";chatJournal.flush();}}
        const bool chatChanged=nativeClient&&chatLog&&chatLog->snapshotIfChanged(chatSnapshot.revision,chatSnapshot);
        if(nativeClient&&chatLog)chatLog->snapshotActiveIfChanged(chatSelected.revision,chatSelected);
        if(nativeClient&&(chatFirstFrame||chatChanged)&&chatJournalEvents<512){++chatJournalEvents;
            std::cout<<"NATIVE_CHAT revision="<<chatSnapshot.revision<<" lines="<<chatSnapshot.lines.size()<<" evicted="<<chatSnapshot.evicted<<" unsupported="<<chatSnapshot.unsupported<<"\n";
            if(chatJournal){chatJournal<<"{\"event\":\"receive\",\"elapsed_ms\":"<<chatElapsed()<<",\"revision\":"<<chatSnapshot.revision<<",\"lines\":"<<chatSnapshot.lines.size()<<",\"evicted\":"<<chatSnapshot.evicted<<",\"unsupported\":"<<chatSnapshot.unsupported;
                if(!chatSnapshot.lines.empty()){const auto& last=chatSnapshot.lines.back();chatJournal<<",\"sender\":"<<last.sender<<",\"chat_target\":"<<last.target<<",\"chat_type\":"<<last.chatType<<",\"mood\":"<<last.mood<<",\"flags\":"<<unsigned(last.flags)<<",\"language\":"<<unsigned(last.language);}
                if(!chatSnapshot.lines.empty()&&chatSnapshot.lines.back().localized){const auto& p=*chatSnapshot.lines.back().localized;
                    chatJournal<<",\"localized\":true,\"di\":"<<p.di<<",\"tu_id\":"<<p.tu.objectId<<",\"tt_id\":"<<p.tt.objectId<<",\"to_id\":"<<p.to.objectId;
                    if(swgproto::isValidStringIdReference(p.id))chatJournal<<",\"stf_file\":\""<<p.id.file<<"\",\"stf_key\":\""<<p.id.stringId<<"\"";
                }
                chatJournal<<"}\n";chatJournal.flush();}}
        if(nativeClient&&chatInputEvents<256&&(chatFirstFrame||lastChatFocus!=window.textFocus().active()||lastDraftUnits!=window.textFocus().draft().size())) {
            lastChatFocus=window.textFocus().active();lastDraftUnits=window.textFocus().draft().size();++chatInputEvents;
            std::cout<<"NATIVE_CHAT_INPUT focused="<<lastChatFocus<<" units="<<lastDraftUnits<<" cursor="<<window.textFocus().cursor()<<"\n";
            if(chatJournal&&chatJournalEvents<512){++chatJournalEvents;chatJournal<<"{\"event\":\"input\",\"elapsed_ms\":"<<chatElapsed()<<",\"focused\":"<<(lastChatFocus?"true":"false")<<",\"units\":"<<lastDraftUnits<<",\"cursor\":"<<window.textFocus().cursor()<<"}\n";chatJournal.flush();}
        }
        chatFirstFrame=false;
        if(window.takeKeyPress(renderer::Window::Escape)) break;
        if(window.width()<=0||window.height()<=0){std::this_thread::sleep_for(std::chrono::milliseconds(15));continue;}
        gfx.resize(window.width(),window.height());
        if (networkThread && networkThread->failed()) {
            failed = true;
            break;
        }

        auto now = Clock::now();
        if(smoke&&std::chrono::duration<float>(now-smokeStart).count()>90)throw std::runtime_error("Offline smoke timed out waiting for real assets");
        float deltaSeconds = std::clamp(std::chrono::duration<float>(now - lastFrameTime).count(),0.0f,0.05f);
        lastFrameTime = now;
        if(smoke&&smokePhase>=0)deltaSeconds=1.0f/30;

        // Phase 18 - one-shot edge-detected toggle (same technique as
        // leftMouseWasDown below), not held-key, so tapping 'I' doesn't
        // flip the mode 30 times/second. Seeds the fly camera's starting
        // position from wherever the follow camera currently is (rather
        // than always resetting to FlyCamera's default {0,15,-15}) so
        // toggling into inspection mode doesn't teleport the view.
        if(window.takeKeyPress('I')) {
            if(!inspectionMode){flyCamera.position=camera.position;flyCamera.yaw=camera.yaw;flyCamera.pitch=camera.pitch;}
            inspectionMode=!inspectionMode;
        }
        if(offline&&window.takeKeyPress('0')) {
            predictedSelfPos=offlineSpawn;predictedSelfYawRadians=DirectX::XM_PI;
            camera.yaw=DirectX::XM_PI;camera.pitch=-.22f;camera.distance=8;
            persistentCellIndex.reset();persistentCellBuildingId=0;cellTraversal.select(0,0);persistentFloorTriangleIndex.reset();
            inspectionMode=false;
            offlinePlayback->reset();offlineRun=false;
        }
        if(offline&&window.takeKeyPress('R'))offlineRun=!offlineRun;
        if(nativeClient&&window.takeKeyPress('R'))liveRun=!liveRun;
        if(nativeClient&&window.takeKeyPress('E')&&livePlayback&&liveStanding&&!liveActionNames.empty())
            livePlayback->playAction(liveActionNames[liveActionIndex++%liveActionNames.size()]);
        if(nativeClient&&window.takeKeyPress('C')&&livePlayback)livePlayback->clearAction();
        if(nativeClient&&window.takeKeyPress('M')&&livePlayback) {
            liveAmbientEligible=!liveAmbientEligible;livePlayback->setAmbientEligible(liveAmbientEligible);
        }
        if(offline&&window.takeKeyPress('E')&&!offlineActionNames.empty()) {
            offlinePlayback->playAction(offlineActionNames[offlineActionIndex++%offlineActionNames.size()]);
        }
        if(offline&&window.takeKeyPress('C'))offlinePlayback->clearAction();
        if(offline&&window.takeKeyPress('M')) {
            offlineAmbientEligible=!offlineAmbientEligible;offlinePlayback->setAmbientEligible(offlineAmbientEligible);
        }
        if(actionSmoke&&smokePhase>=0) {
            if(smokeFrames==20)offlinePlayback->playAction("rub_chin_thoughtful_ag");
            if(smokeFrames==22)offlinePlayback->playAction("standing_idle_ambient_3_ag");
            if(smokeFrames==52)offlinePlayback->clearAction();
            if(smokeFrames==70)offlinePlayback->playAction("look_casual_ag");
        }
        if(offline||nativeClient) {
            int dx=0,dy=0;window.takeMouseDragDelta(0,dx,dy);
            auto& yaw=inspectionMode?flyCamera.yaw:camera.yaw;
            auto& pitch=inspectionMode?flyCamera.pitch:camera.pitch;
            yaw+=dx*camera.mouseSensitivity;pitch=std::clamp(pitch-dy*camera.mouseSensitivity,-DirectX::XM_PIDIV2+.01f,DirectX::XM_PIDIV2-.01f);
        }

        // Locked-camera/burst-capture keys (K/J/P) - see ScreenshotCapture's
        // own class comment. Not meaningful in inspection mode (that uses
        // the free FlyCamera instead), so this only affects the normal
        // FollowCamera path.
        if(!offline)screenshotCapture.processFrameInput();

        // Animation-composition debug keys (B/V/C/T/N/Z/R/X/L/F/G/H/U/M) -
        // see AnimationDebugControls's own class comment for what each one
        // tests and why they're kept as permanent debug aids.
        if(!offline&&(!nativeClient||autoTest.enabled()))animControls.processFrameInput(selfClipCache);
        if (autoTest.enabled()) {
            autoTest.driveFrame(animControls, screenshotCapture);
        }

        // One snapshot per frame, reused by every resolveWorldPosition() call
        // below (self-seeding, draw pass, label pass) - see
        // resolveWorldPosition()'s own comment for why it can't call back
        // into the live ObjectStore from inside a forEach() callback.
        if(cellSmoke)for(auto& obj:offlineObjects)std::visit([&](auto& o){
            if(o.objectId==500){bool active=smokeFrames>=430&&smokeFrames<460;auto p=offlineBuildingFrame.toWorld({active?cellDoorCenter.x:90,0,active?cellDoorCenter.z:90});o.x=p.x;o.z=p.z;}
        },obj);
        std::unordered_map<uint64_t,worldmodel::WorldObject> worldSnapshot;
        std::vector<worldmodel::ServerCellBinding> serverCells;
        forEachObject([&](const auto& obj){
            worldSnapshot.emplace(obj.objectId,obj);
            if constexpr(std::is_same_v<std::decay_t<decltype(obj)>,worldmodel::CellObject>) {
                if(obj.base3&&obj.containmentMessagesSeen&&obj.containerId&&obj.base3->cellNumber>0)
                    serverCells.push_back({obj.objectId,obj.containerId,obj.base3->cellNumber});
            }
        });


        if(frameJournal&&frameJournalEvents<4000&&movementMillis(Clock::now())>=frameSceneNext) {
            frameSceneNext=movementMillis(Clock::now())+5000;size_t cellsSeen=0,numbered=0,contained=0,spatial=0,precise=0,compact=0,invalidRotation=0;
            const auto sceneStats=objectStore?objectStore->sceneMaterializationStats():worldmodel::SceneMaterializationStats{};
            size_t validParentFrames=0,loadedCells=0,loadedFloors=0;
            for(auto cell:serverCells) {
                auto parent=worldSnapshot.find(cell.buildingId);
                if(parent==worldSnapshot.end()||!worldmodel::objectLocalTransform(parent->second))continue;
                ++validParentFrames;auto geometry=buildingCache.get(parent->second.objectCrc,assetWorker);
                if(!geometry||cell.number<=0||size_t(cell.number)>=geometry->size())continue;
                const auto& room=(*geometry)[cell.number];
                if(room.bounds&&!room.submeshes.empty())++loadedCells;
                if(!room.floorMesh.positions.empty())++loadedFloors;
            }
            forEachObject([&](const auto& obj){
                if(obj.parentId)++spatial;
                if(obj.transformMessagesSeen){if(obj.fullRotationCurrent)++precise;else ++compact;if(!worldmodel::objectRotation(obj))++invalidRotation;}
                if constexpr(std::is_same_v<std::decay_t<decltype(obj)>,worldmodel::CellObject>) {
                    ++cellsSeen;if(obj.base3)++numbered;if(obj.containmentMessagesSeen&&obj.containerId)++contained;
                }
            });
            ++frameJournalEvents;frameJournal<<"{\"event\":\"scene_frames\",\"elapsed_ms\":"<<movementMillis(Clock::now())<<",\"objects\":"<<worldSnapshot.size()
                <<",\"cell_objects\":"<<cellsSeen<<",\"cells_with_baseline3\":"<<numbered<<",\"cells_with_building_container\":"<<contained
                <<",\"resolved_cell_maps\":"<<serverCells.size()<<",\"cells_with_valid_parent_frame\":"<<validParentFrames<<",\"cells_with_loaded_geometry\":"<<loadedCells<<",\"cells_with_loaded_floor\":"<<loadedFloors
                <<",\"spatial_parented_objects\":"<<spatial<<",\"full_rotation_objects\":"<<precise<<",\"byte_rotation_objects\":"<<compact<<",\"invalid_rotation_objects\":"<<invalidRotation<<",\"pending_position_seeds\":"<<sceneStats.pendingPositionSeeds
                <<",\"pending_containments\":"<<sceneStats.pendingContainments<<",\"containment_updates\":"<<sceneStats.containmentUpdates<<",\"containment_before_baseline\":"<<sceneStats.containmentBeforeBaseline
                <<",\"containment_objects_applied_from_pending\":"<<sceneStats.containmentObjectsAppliedFromPending<<",\"containment_pending_replacements\":"<<sceneStats.containmentPendingReplacements
                <<",\"containment_pending_dropped\":"<<sceneStats.containmentPendingDropped<<",\"position_seeds_dropped\":"<<sceneStats.positionSeedsDropped<<"}\n";frameJournal.flush();
        }
        if(cellJournal&&cellJournalEvents<4000)for(auto c:serverCells) {
            auto building=worldSnapshot.find(c.buildingId);
            auto observed=objectStore?objectStore->sceneBaselineObservation(c.buildingId):std::nullopt;
            auto key=std::tuple{c.buildingId,c.number,building!=worldSnapshot.end()?building->second.transformMessagesSeen:0,
                building!=worldSnapshot.end(),observed?observed->views:0};
            if(journalCellsSeen.contains(c.objectId)&&journalCellsSeen[c.objectId]==key)continue;
            journalCellsSeen[c.objectId]=key;++cellJournalEvents;
            cellJournal<<"{\"event\":\"cell_map\",\"elapsed_ms\":"<<movementMillis(Clock::now())<<",\"parent_id\":"<<c.objectId
                <<",\"building_id\":"<<c.buildingId<<",\"number\":"<<c.number;
            auto frame=building!=worldSnapshot.end()?worldmodel::objectLocalTransform(building->second):std::nullopt;
            cellJournal<<",\"building_frame_valid\":"<<(frame?"true":"false")<<",\"building_present\":"<<(building!=worldSnapshot.end()?"true":"false");
            if(observed)cellJournal<<",\"building_baseline_type\":"<<observed->objectType<<",\"building_baseline_views\":"<<observed->views<<",\"building_baseline_messages\":"<<observed->messages;
            auto pending=objectStore?objectStore->pendingSceneSeed(c.buildingId):std::nullopt;
            if(pending)cellJournal<<",\"building_pending_seed_crc\":"<<pending->objectCrc;
            if(frame)cellJournal<<",\"building_origin\":["<<frame->origin.x<<","<<frame->origin.y<<","<<frame->origin.z<<"],\"direction\":"<<int(building->second.direction)<<",\"object_crc\":"<<building->second.objectCrc
                <<",\"full_rotation_current\":"<<(building->second.fullRotationCurrent?"true":"false")<<",\"quaternion\":["<<frame->rotation.x<<","<<frame->rotation.y<<","<<frame->rotation.z<<","<<frame->rotation.w<<"]";
            cellJournal<<"}\n";cellJournal.flush();
        }

        // Update one immutable presentation snapshot per frame, shared by
        // geometry, labels, picking and minimap. Self stays locally predicted.
        std::unordered_map<uint64_t,clientcommon::RemotePose> remotePoses;
        const auto presentationMs=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        if(nativeClient) {
            for(auto it=remotePresentation.begin();it!=remotePresentation.end();) {
                if(!worldSnapshot.contains(it->first))it=remotePresentation.erase(it);else ++it;
            }
            for(const auto& [id,obj]:worldSnapshot) {
                if(obj.isSelf||obj.typeTag!=worldmodel::ObjectTypeTag::Creature||!obj.transformMessagesSeen)continue;
                const auto resolved=resolveWorldPosition(worldSnapshot,obj);
                if(!resolved)continue;
                auto found=remotePresentation.find(id);
                if(found==remotePresentation.end()) {
                    if(remotePresentation.size()>=8192)continue;
                    found=remotePresentation.emplace(id,RemotePresentationState{}).first;
                }
                auto& state=found->second;
                if(state.revision!=obj.transformMessagesSeen) {
                    float yaw=(float(obj.direction)/100.f)*DirectX::XM_2PI;
                    if(auto pose=worldmodel::resolveWorldTransform(worldSnapshot,obj))yaw=worldmodel::rotationHeading(pose->rotation);

                    const auto receivedMs=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(obj.lastTransformUpdate.time_since_epoch()).count());
                    state.motion.update({{resolved->x,resolved->y,resolved->z,yaw},obj.parentId,receivedMs,obj.movementCounter,
                        state.authoritativeGeneration!=obj.authoritativeTransformMessagesSeen},presentationMs);
                    state.revision=obj.transformMessagesSeen;
                    state.authoritativeGeneration=obj.authoritativeTransformMessagesSeen;
                }
                if(auto pose=state.motion.sample(presentationMs))remotePoses.emplace(id,*pose);
            }
        }

        if(nativeClient)for(const auto& [id,obj]:worldSnapshot) {
            if(obj.typeTag!=worldmodel::ObjectTypeTag::Creature||!obj.transformMessagesSeen)continue;
            auto it=journalTransformSeen.find(id);
            if(it!=journalTransformSeen.end()&&it->second==obj.transformMessagesSeen)continue;
            if(it==journalTransformSeen.end()&&journalTransformSeen.size()>=8192)continue;
            journalTransformSeen[id]=obj.transformMessagesSeen;
            journalMovement(obj.isSelf?"self_server":"remote_server",id,obj.parentId,obj.movementCounter,obj.x,obj.y,obj.z,obj.speed);
        }
        if(nativeClient&&offlineTitleReady&&clientcommon::takeMovementDiagnosticRequest()&&!movementTestActive&&!inspectionMode) {
            movementTestActive=true;movementTestStart=now;movementTestYaw=predictedSelfYawRadians;
            std::cout<<"NATIVE_MOVEMENT_TEST started\n";
        }
        if(movementTestActive&&std::chrono::duration<float>(now-movementTestStart).count()>=36) {
            movementTestActive=false;std::cout<<"NATIVE_MOVEMENT_TEST complete\n";
        }
        forEachObject([&](const auto& obj) {
            if constexpr(std::is_same_v<std::decay_t<decltype(obj)>,worldmodel::CreatureObject>) {
                if(nativeClient&&obj.isSelf) {
                    liveStanding=!obj.base3||obj.base3->posture==0;
                    if(obj.base4) {
                        const auto& movement=*obj.base4;
                        if(std::isfinite(movement.walkSpeed)&&movement.walkSpeed>0&&movement.walkSpeed<=50)liveWalkSpeed=movement.walkSpeed;
                        if(std::isfinite(movement.runSpeed)&&movement.runSpeed>0&&movement.runSpeed<=50)liveRunSpeed=movement.runSpeed;
                    }
                }
            }
            if (obj.isSelf && obj.transformMessagesSeen > 0) {
                auto resolved=resolveWorldPosition(worldSnapshot,obj);
                if(nativeClient&&journalSelfSyncGeneration!=obj.authoritativeTransformMessagesSeen) {
                    journalSelfSyncGeneration=obj.authoritativeTransformMessagesSeen;
                    journalMovement("self_sync",obj.objectId,obj.parentId,obj.serverDataTransformCounter,obj.x,obj.y,obj.z,obj.speed);
                }

                auto correction=clientcommon::CorrectionDecision::KeepPrediction;
                if(nativeClient) {
                    correction=selfCorrectionTracker.observe(obj.transformMessagesSeen,obj.authoritativeTransformMessagesSeen,
                        movementReporter.correctionIsCurrent(obj.serverDataTransformCounter),resolved&&std::isfinite(resolved->x)&&std::isfinite(resolved->y)&&std::isfinite(resolved->z));
                    if(correction==clientcommon::CorrectionDecision::IgnoreStaleSnapshot)return;
                }
                if(auto pose=worldmodel::resolveWorldTransform(worldSnapshot,obj))
                    lastKnownSelfYawRadians=worldmodel::rotationHeading(pose->rotation);
                selfObjectId = obj.objectId;
                bool parentChanged = obj.parentId != predictedSelfParentId;
                // Real containment status tracked regardless of whether
                // resolveWorldPosition() could fully resolve it this frame
                // - this alone decides indoor-vs-outdoor height handling
                // below, and must stay correct even while unresolved so
                // that branch doesn't wrongly fall back to outdoor terrain
                // height while genuinely indoors.
                if(!serverCellTracking||!predictedSelfInitialized)predictedSelfParentId=obj.parentId;
                if (resolved.has_value()) {
                    // Only snap/track position from a SUCCESSFULLY resolved
                    // value - see resolveWorldPosition()'s own comment for
                    // why silently accepting an unresolved fallback (raw
                    // local coordinates treated as world-space) was a real,
                    // live-caught bug ("floating, ignoring terrain, near
                    // world origin"). If unresolved, predictedSelfPos and
                    // lastKnownSelfPos both simply hold their last known
                    // good values untouched - a stall, not a wrong jump.
                    lastKnownSelfPos = *resolved;
                    if (!predictedSelfInitialized) {
                        predictedSelfPos = lastKnownSelfPos;
                        predictedSelfYawRadians = lastKnownSelfYawRadians;
                        predictedSelfInitialized = true;
                    } else if (nativeClient&&correction==clientcommon::CorrectionDecision::ApplyAuthoritative) {
                        predictedSelfPos=lastKnownSelfPos;
                        predictedSelfParentId=obj.parentId;
                        persistentCellIndex.reset();persistentCellBuildingId=0;cellTraversal.select(0,0);
                        persistentFloorTriangleIndex.reset();persistentFloorTriangleCellIndex.reset();persistentFloorTriangleBuildingId=0;
                        predictedSelfYawRadians=lastKnownSelfYawRadians;
                        movementReporter.adoptCounter(obj.serverDataTransformCounter);
                        journalMovement("self_correction",obj.objectId,obj.parentId,obj.serverDataTransformCounter,predictedSelfPos.x,predictedSelfPos.y,predictedSelfPos.z,obj.speed);
                    } else if (parentChanged&&!serverCellTracking) {
                        // A real cell transition (entered/left a building,
                        // rode the elevator, walked into a different room) -
                        // snap rather than keep predicting through it, see
                        // predictedSelfParentId's own comment above.
                        predictedSelfPos = lastKnownSelfPos;
                    }

                }
            }
        });

        // Player movement (Phase 13) - camera-relative WASD: forward/back
        // along the camera's own current look direction (projected onto the
        // XZ plane - vertical look angle shouldn't tilt movement), strafe
        // perpendicular to it. Character always faces its movement
        // direction (confirmed with the user over a classic SWG-style
        // turn-in-place scheme). Only runs once self's real starting
        // position is known - there's nothing to move from before that.
        // Skipped entirely in inspection mode (Phase 18): WASD drives the
        // free fly camera instead (via FlyCamera::update() below), not self
        // - self simply stays put while the camera roams.
        selfIsMoving=false;
        if (predictedSelfInitialized && !inspectionMode) {
            DirectX::XMFLOAT3 lookDir = camera.lookDirection();
            DirectX::XMVECTOR forwardVec = DirectX::XMVector3Normalize(
                DirectX::XMVectorSet(lookDir.x, 0.0f, lookDir.z, 0.0f));
            DirectX::XMVECTOR worldUpVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            // Same right-vector convention already used by the label-
            // billboard basis below (cross(worldUp, forward)).
            DirectX::XMVECTOR rightVec =
                DirectX::XMVector3Normalize(DirectX::XMVector3Cross(worldUpVec, forwardVec));

            DirectX::XMVECTOR moveVec = DirectX::XMVectorZero();
            bool movementKeyHeld = false;
            auto movementKeyDown=[&](int key){bool tap=window.takeKeyPress(key);return renderer::Window::isKeyDown(key)||tap;};
            if (movementKeyDown('W')||(smoke&&!actionSmoke&&!cameraSmoke&&!floorSmoke&&!cellSmoke&&(smokePhase==1||smokePhase==3))) {
                moveVec = DirectX::XMVectorAdd(moveVec, forwardVec);
                movementKeyHeld = true;
            }
            if (movementKeyDown('S')) {
                moveVec = DirectX::XMVectorSubtract(moveVec, forwardVec);
                movementKeyHeld = true;
            }
            if (movementKeyDown('D')) {
                moveVec = DirectX::XMVectorAdd(moveVec, rightVec);
                movementKeyHeld = true;
            }
            if (movementKeyDown('A')) {
                moveVec = DirectX::XMVectorSubtract(moveVec, rightVec);
                movementKeyHeld = true;
            }
            if(movementTestActive) {
                const float seconds=std::chrono::duration<float>(now-movementTestStart).count();
                const float direction=seconds<1.2f?1.f:(seconds>=2.f&&seconds<3.2f?-1.f:0.f);
                moveVec=DirectX::XMVectorSet(std::sin(movementTestYaw)*direction,0,std::cos(movementTestYaw)*direction,0);
                movementKeyHeld=direction!=0;
            }
            if(DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(moveVec))<1e-6f)movementKeyHeld=false;
            selfIsMoving = movementKeyHeld; // Phase 21 - persists past this block for animation state selection
            const float requestedMovementSpeed=nativeClient?(liveRun?liveRunSpeed:liveWalkSpeed):walkSpeedMetersPerSecond;
            if(nativeClient&&livePlayback&&liveStanding)livePlayback->advance(deltaSeconds,movementKeyHeld?requestedMovementSpeed:0);
            assets::Float3 offlineStep{};
            if(offlinePlayback)offlineStep=offlinePlayback->advance(deltaSeconds,
                movementKeyHeld?(offlineRun?offlinePlayback->runningSpeed():offlinePlayback->walkingSpeed()):0);

            // Real collision (Phase 20) needs both the pre-move and
            // candidate post-move world position - captured here, before
            // the WASD delta below mutates predictedSelfPos.x/z in place.
            DirectX::XMFLOAT3 preMovePos = predictedSelfPos;

            float currentSpeed = 0.0f;
            if (movementKeyHeld) {
                moveVec = DirectX::XMVector3Normalize(moveVec);
                DirectX::XMFLOAT3 moveDir;
                DirectX::XMStoreFloat3(&moveDir, moveVec);

                assets::Float3 localStep{0,0,requestedMovementSpeed*deltaSeconds};
                if(offlinePlayback) {
                    // Consume the same frame interval that will be drawn below.
                    // LOCT carries lateral weight shift and stride-wise speed;
                    // average speed alone discards both. Ground queries own Y.
                    localStep=offlineStep;
                }
                const float stepX=moveDir.x*localStep.z+moveDir.z*localStep.x;
                const float stepZ=moveDir.z*localStep.z-moveDir.x*localStep.x;
                predictedSelfPos.x += stepX;
                predictedSelfPos.z += stepZ;
                if(smoke&&smokePhase==1&&smokeFrames>=8){smokeExpectedWalkDelta.x+=stepX;smokeExpectedWalkDelta.z+=stepZ;}
                // Same sin/cos(yaw) convention as FlyCamera::forward()/
                // FollowCamera::lookDirection() and the minimap facing
                // indicator below - yaw=0 faces +Z, increasing yaw rotates
                // toward +X.
                predictedSelfYawRadians = std::atan2(moveDir.x, moveDir.z);
                if(smoke&&smokePhase==1)smokeWalkYawRadians=predictedSelfYawRadians;
                currentSpeed = requestedMovementSpeed;
                if(smoke&&smokePhase==1)++smokeWalkMovementFrames;
            }

            if(cellSmoke&&smokePhase>=0) {
                float offset=2;
                if(smokeFrames>=90&&smokeFrames<170)offset=2-float(smokeFrames-90)*.05f;
                else if(smokeFrames>=170&&smokeFrames<260)offset=-2;
                else if(smokeFrames>=260&&smokeFrames<340)offset=-2+float(smokeFrames-260)*.05f;
                else if(smokeFrames>=430&&smokeFrames<460)offset=-2;
                auto candidate=offlineBuildingFrame.toWorld({cellDoorCenter.x,0,cellDoorCenter.z+offset});
                predictedSelfPos.x=candidate.x;predictedSelfPos.z=candidate.z;
                selfIsMoving=(smokeFrames>=90&&smokeFrames<170)||(smokeFrames>=260&&smokeFrames<340)||(smokeFrames>=430&&smokeFrames<460);
                movementKeyHeld=selfIsMoving;currentSpeed=selfIsMoving?1.5f:0;
                predictedSelfYawRadians=(smokeFrames<260?DirectX::XM_PI:0)+worldmodel::rotationHeading(offlineBuildingFrame.rotation);
            }
            if(floorSmoke&&smokePhase>=0) {
                // Candidate-position replay through the real movement/support
                // pass. This is not a time-based gait or server traversal test.
                float z=36.5f;
                if(smokeFrames>=90&&smokeFrames<460)z-=float(smokeFrames-90)*.05f;
                else if(smokeFrames>=460&&smokeFrames<830)z=18.f+float(smokeFrames-460)*.05f;
                predictedSelfPos.x=offlineBuildingOrigin.x;predictedSelfPos.z=offlineBuildingOrigin.z+z;
                selfIsMoving=smokeFrames>=90&&smokeFrames<830;
                predictedSelfYawRadians=smokeFrames<460?DirectX::XM_PI:0;
            }

            // Propose portal crossings against the accepted pre-move cell.
            // Commit both rendering cell and outbound parent only after all
            // structural and curated-object collision checks accept the move.
            // Server relocations can separately reseed this state above.
            //
            // The specific cell is looked up using the PRE-move position
            // (self was validly there a moment ago, guaranteed), not the
            // just-computed candidate - a real wall-clip attempt can put
            // the candidate position on the far side of a wall, outside
            // every cell's own bounds, which would otherwise make "which
            // cell's collision mesh applies" ambiguous right when it
            // matters most.
            bool selfInsideAnyBuildingFootprint = false;
            bool movementBlockedByWall = false;
            // Real bugfix, found live: queryFloorHeight() returns a height
            // in the BUILDING-LOCAL frame (it raycasts against the
            // collision mesh's own local vertices, same space as oldLocal/
            // newLocal) - it must be converted back to WORLD space (+
            // buildingCandidate.y, captured here as buildingWorldY) before
            // ever being written to predictedSelfPos.y, which is always
            // world space. Missing that conversion was the real root cause
            // of the live-observed stuttering/oscillation and "walks
            // through stairs" behavior: a real local floor hit (e.g. 9.5)
            // was being written directly into predictedSelfPos.y as if it
            // were already a world Y, teleporting self to a wildly wrong
            // world height every time a floor hit was found. That wrong
            // world Y then failed the NEXT frame's containment test
            // (worldToBuildingLocal correctly subtracts building.y, so the
            // now-wrong world Y produced local coordinates outside every
            // real cell's bounds), falling back to the stale spawn-time
            // lastKnownSelfPos.y, re-entering the room, computing the same
            // local floor height again, and repeating forever.
            std::optional<worldmodel::CellCrossing> proposedCellCrossing;
            std::optional<uint64_t> proposedCellParent;
            const auto oldFloorTriangle=persistentFloorTriangleIndex,oldFloorCell=persistentFloorTriangleCellIndex;
            const auto oldFloorBuilding=persistentFloorTriangleBuildingId;
            std::optional<worldmodel::ServerCellBinding> activeServerCell;
            if(predictedSelfParentId)for(auto c:serverCells)if(c.objectId==predictedSelfParentId)activeServerCell=c;
            if(serverCellTracking&&predictedSelfParentId&&!activeServerCell)movementBlockedByWall=true;
            if(serverCellTracking&&predictedSelfParentId&&activeServerCell) {
                auto building=worldSnapshot.find(activeServerCell->buildingId);
                if(building==worldSnapshot.end()||!worldmodel::objectLocalTransform(building->second)||!buildingFrame(building->second).preservesWorldUp())movementBlockedByWall=true;
                else {
                    const auto* knownCells=buildingCache.get(building->second.objectCrc,assetWorker);
                    if(!knownCells||activeServerCell->number<=0||size_t(activeServerCell->number)>=knownCells->size()||!(*knownCells)[0].bounds)movementBlockedByWall=true;
                }
            }
            std::optional<float> newFloorHeight;
            float buildingWorldY = 0.0f;
            float exteriorSupportDelta=std::numeric_limits<float>::infinity();
            const char* floorSource="hold";uint64_t floorOwner=0;size_t floorBuriedCandidates=0;
            forEachObject([&](const auto& buildingCandidate) {
                using T = std::decay_t<decltype(buildingCandidate)>;
                if constexpr (std::is_same_v<T, worldmodel::CellObject> ||
                              std::is_same_v<T, worldmodel::GroupObject>) {
                    return;
                } else {
                    if (selfInsideAnyBuildingFootprint || buildingCandidate.isSelf ||
                        buildingCandidate.typeTag == worldmodel::ObjectTypeTag::Creature ||
                        buildingCandidate.typeTag == worldmodel::ObjectTypeTag::Player) {
                        return;
                    }
                    if(serverCellTracking&&predictedSelfParentId&&(!activeServerCell||activeServerCell->buildingId!=buildingCandidate.objectId))return;
                    auto frame=worldmodel::objectLocalTransform(buildingCandidate);
                    if(!frame)return;
                    const auto* cells = buildingCache.get(buildingCandidate.objectCrc, assetWorker);

                    if (cells == nullptr || cells->empty() || !(*cells)[0].bounds.has_value()) {
                        return;
                    }
                    const auto& b = *(*cells)[0].bounds;
                    DirectX::XMFLOAT3 oldLocal = worldToBuildingLocal(preMovePos, buildingCandidate);
                    DirectX::XMFLOAT3 newLocal =
                        worldToBuildingLocal(predictedSelfPos, buildingCandidate);

                    // Broad phase includes exterior ramp candidates. The wide
                    // ring is only an asset search area; it never permits a
                    // nearest-point floor height beyond triangle coverage.
                    // Wall/cell tracking keeps the tighter footprint below.
                    bool outsideWideFootprint = oldLocal.x < b.min.x - kFloorFallbackMarginMeters ||
                        oldLocal.x > b.max.x + kFloorFallbackMarginMeters ||
                        oldLocal.y < b.min.y - kFloorFallbackMarginMeters ||
                        oldLocal.y > b.max.y + kFloorFallbackMarginMeters ||
                        oldLocal.z < b.min.z - kFloorFallbackMarginMeters ||
                        oldLocal.z > b.max.z + kFloorFallbackMarginMeters;
                    if (outsideWideFootprint) {
                        return;
                    }
                    bool outsideTightFootprint = oldLocal.x < b.min.x - kCellBoundsMarginMeters ||
                        oldLocal.x > b.max.x + kCellBoundsMarginMeters ||
                        oldLocal.y < b.min.y - kCellBoundsMarginMeters ||
                        oldLocal.y > b.max.y + kCellBoundsMarginMeters ||
                        oldLocal.z < b.min.z - kCellBoundsMarginMeters ||
                        oldLocal.z > b.max.z + kCellBoundsMarginMeters;

                    // Arbitrarily tilted frames render correctly, but the
                    // upright floor controller must not invent a tilted walk.
                    if(!frame->preservesWorldUp()){if(!outsideTightFootprint)movementBlockedByWall=true;return;}
                    const CellHandles& shell = (*cells)[0];
                    // Default: the unblocked candidate position - the
                    // ramp-only branch below never computes a `blocked`
                    // flag (no wall test happens out there), so this stays
                    // as-is for that case; the tight-footprint branch
                    // overwrites it once `blocked` is known.
                    DirectX::XMFLOAT3 floorQueryPos = newLocal;
                    // Interior floor continuity remains adjacency based. A cursor
                    // belongs to one building instance and one cell, never just
                    // a cell index that another nearby building can reuse.
                    auto tryFloorHeight = [&](const CellHandles& source, size_t sourceCellIndex) {
                        if (!source.floorMesh.positions.empty()) {
                            std::optional<FloorHit2D> hit2D;
                            if (persistentFloorTriangleIndex && persistentFloorTriangleCellIndex &&
                                persistentFloorTriangleBuildingId==buildingCandidate.objectId &&
                                *persistentFloorTriangleCellIndex==sourceCellIndex) {
                                hit2D=queryFloorHeight2DAdjacent(source.floorMesh,*persistentFloorTriangleIndex,floorQueryPos);
                            }
                            if(!hit2D)hit2D=queryFloorHeight2DFullScan(source.floorMesh,floorQueryPos,floorQueryPos.y);
                            if(hit2D) {
                                newFloorHeight=hit2D->y;
                                persistentFloorTriangleIndex=hit2D->triangleIndex;
                                persistentFloorTriangleCellIndex=sourceCellIndex;
                                persistentFloorTriangleBuildingId=buildingCandidate.objectId;
                                floorSource="interior_floor";floorOwner=buildingCandidate.objectId;
                                return;
                            }
                        }
                        if(auto hit=queryFloorHeight(source.collisionMesh,floorQueryPos)) {
                            newFloorHeight=hit;floorSource="interior_collision";floorOwner=buildingCandidate.objectId;
                        }
                    };
                    auto tryExteriorFloor = [&](bool ownsFootprint) {
                        // Terrain and query use the same building-local Y frame.
                        std::optional<float> terrainLocal;
                        const auto worldQuery=buildingLocalToWorld(floorQueryPos,buildingCandidate);
                        if(terrainSource)terrainLocal=terrainSource->querySurfaceHeight(worldQuery.x,worldQuery.z)-buildingCandidate.y;
                        auto support=worldmodel::queryExteriorFloorSupport(shell.floorMesh,shell.collisionMesh,
                            {floorQueryPos.x,floorQueryPos.y,floorQueryPos.z},terrainLocal);
                        floorBuriedCandidates+=support.buriedCandidates;
                        const bool meshSupport=support.source==worldmodel::ExteriorFloorSupport::Source::FloorMesh ||
                            support.source==worldmodel::ExteriorFloorSupport::Source::CollisionMesh;
                        const float worldHeight=buildingCandidate.y+support.height;
                        const float delta=std::abs(worldHeight-predictedSelfPos.y);
                        if(ownsFootprint || (meshSupport&&delta<exteriorSupportDelta)) {
                            // Commit height together with its owning transform.
                            // A rejected neighboring instance cannot overwrite Y.
                            newFloorHeight=support.height;buildingWorldY=buildingCandidate.y;
                            exteriorSupportDelta=delta;floorOwner=buildingCandidate.objectId;
                            floorSource=worldmodel::exteriorFloorSourceName(support.source);
                        }
                    };

                    if (outsideTightFootprint) {
                        // Broad phase finds exterior ramps; it grants no floor
                        // extrapolation and does not establish an interior cell.
                        tryExteriorFloor(false);
                        return;
                    }

                    selfInsideAnyBuildingFootprint = true;
                    // This instance owns the tight footprint. Discard any outer
                    // ring candidate selected earlier with a different transform.
                    newFloorHeight.reset();floorOwner=0;floorSource="hold";
                    buildingWorldY = buildingCandidate.y;

                    DirectX::XMFLOAT3 wallTestFrom{oldLocal.x, oldLocal.y + kWallTestHeightMeters,
                                                    oldLocal.z};
                    DirectX::XMFLOAT3 wallTestTo{newLocal.x, newLocal.y + kWallTestHeightMeters,
                                                  newLocal.z};

                    // Real bugfix, found live (Phase 20b): the old AABB-only
                    // findContainingCellIndex() was re-run from scratch every
                    // single frame, and cell boundaries are exactly where its
                    // padded-bounding-box approximation is weakest (real
                    // rooms are rarely axis-aligned boxes; a stairwell is a
                    // continuous ramp, not a box at all) - confirmed live as
                    // the root cause of recurring boundary flakiness
                    // (sinking on approach, "hard edge" snaps, stairs never
                    // registering height changes). Real per-cell portal data
                    // (see assets::BuildingCell::portals' own comment) gives
                    // an exact, precise transition signal instead: which
                    // cell self is in is now PERSISTENT state
                    // (persistentCellIndex, reset only on a genuine fresh
                    // entry - see its own comment), only ever changing when
                    // self's own movement segment actually crosses one of
                    // the CURRENT cell's real portal shapes into the
                    // connected adjacent cell. The AABB test still runs, but
                    // only once, as the one-time initial guess for a fresh
                    // entry - never as the ongoing per-frame source of
                    // truth.
                    if (!wasInsideBuildingFootprintLastFrame ||
                        persistentCellBuildingId != buildingCandidate.objectId) {
                        persistentCellIndex.reset();
                        persistentFloorTriangleIndex.reset();persistentFloorTriangleCellIndex.reset();
                        persistentFloorTriangleBuildingId=0;
                        persistentCellBuildingId = buildingCandidate.objectId;
                    }
                    if (!persistentCellIndex.has_value()) {
                        if(serverCellTracking) {
                            if(activeServerCell&&activeServerCell->buildingId==buildingCandidate.objectId&&size_t(activeServerCell->number)<cells->size())
                                persistentCellIndex=size_t(activeServerCell->number);
                            else persistentCellIndex=0;
                        } else persistentCellIndex = findContainingCellIndex(*cells, oldLocal);
                        if (!persistentCellIndex.has_value()) {
                            // Real bugfix, found live: standing on the
                            // building's own ROOF (real geometry that's part
                            // of cell 0/the exterior shell, not any interior
                            // room) has no interior cell whose bounds
                            // contain it - findContainingCellIndex()
                            // deliberately excludes cell 0 itself (correct
                            // for its OTHER caller, room-highlight rendering
                            // - see its own comment), so this always
                            // returned nullopt there. For seeding the
                            // PERSISTENT cell specifically, cell 0 is a
                            // legitimate starting point in its own right -
                            // it has real CMSH floor geometry AND its own
                            // real portal list (confirmed live: without
                            // this fallback, self on the real roof got
                            // seeded into a wrong, unrelated interior room
                            // via the same footprint-AABB match used
                            // elsewhere, then got permanently stuck there
                            // since that wrong room had no real portal path
                            // back to where self actually was).
                            persistentCellIndex = 0;
                        }
                    }
                    auto cellIndex=persistentCellIndex;
                    bool unresolvedCrossing=false;
                    if(cellIndex&&*cellIndex<cells->size()) {
                        cellTraversal.select(buildingCandidate.objectId,*cellIndex);
                        const auto& current=(*cells)[*cellIndex];
                        // The body center crosses the door opening; foot height
                        // remains owned by the floor query. No sphere aperture.
                        auto cross=cellTraversal.propose(current.portals,current.portalShapes,
                            {oldLocal.x,oldLocal.y+kWallTestHeightMeters,oldLocal.z},
                            {newLocal.x,newLocal.y+kWallTestHeightMeters,newLocal.z},cells->size());
                        if(cross) {
                            auto parent=worldmodel::resolveCellParent(serverCells,buildingCandidate.objectId,cross->destinationCell);
                            if(serverCellTracking&&!parent){unresolvedCrossing=true;journalCell(*cross,predictedSelfParentId,"unresolved");}
                            else {
                                proposedCellCrossing=cross;proposedCellParent=parent;
                                cellIndex=cross->destinationCell;
                            }
                        }
                    }
                    bool blocked = unresolvedCrossing;
                    if(proposedCellCrossing&&persistentCellIndex&&*persistentCellIndex<cells->size()) {
                        const auto& source=(*cells)[*persistentCellIndex];
                        if(segmentBlockedByCollisionMesh(source.collisionMesh,wallTestFrom,wallTestTo))blocked=true;
                    }
                    if (cellIndex.has_value()) {
                        const CellHandles& cell = (*cells)[*cellIndex];
                        if (!cell.collisionMesh.positions.empty() &&
                            segmentBlockedByCollisionMesh(cell.collisionMesh, wallTestFrom,
                                                           wallTestTo)) {
                            blocked = true;
                        }
                    }

                    // Real bugfix, found live: cell 0 (the exterior shell)
                    // carries the true outer-wall geometry - an interior
                    // room's own CMSH only covers its own internal
                    // partitions, never the true perimeter wall, so the one
                    // frame self's movement actually crossed that wall was
                    // going untested (using only the interior room's own
                    // mesh, which has no triangle there), letting self
                    // tunnel straight through. Checked unconditionally, not
                    // only when no interior cell matches, so the crossing
                    // frame itself is always caught rather than one frame
                    // late. Deliberately NEVER used for floor height below
                    // (see that comment) - blocking doesn't write to Y, so
                    // this can't cause the height-lock feedback bug height
                    // queries did.
                    if (!shell.collisionMesh.positions.empty() &&
                        segmentBlockedByCollisionMesh(shell.collisionMesh, wallTestFrom,
                                                       wallTestTo)) {
                        blocked = true;
                    }

                    if (blocked) {
                        movementBlockedByWall = true;
                    }

                    floorQueryPos = blocked ? oldLocal : newLocal;
                    if(blocked)cellIndex=persistentCellIndex;
                    if(cellIndex && *cellIndex>0) {
                        // True interior cells keep floor tracking, including
                        // below-terrain basements and above-terrain upper floors.
                        tryFloorHeight((*cells)[*cellIndex],*cellIndex);
                        if(!newFloorHeight)tryFloorHeight(shell,0);
                    } else {
                        tryExteriorFloor(true);
                    }
                }
            });

            // Phase 20b - captures THIS frame's final footprint state for
            // next frame's persistent-cell reset check (see
            // persistentCellIndex's own comment) - must run after the
            // forEach above, once selfInsideAnyBuildingFootprint is settled
            // for the frame.
            wasInsideBuildingFootprintLastFrame = selfInsideAnyBuildingFootprint;

            // Phase 20, Step 4 - the small curated non-structural object
            // list (bazaar/bank terminals). Only checked if the wall pass
            // above didn't already block this move - no need to resolve
            // curated-object positions on an already-rejected move. Uses
            // the same worldSnapshot taken once per frame, per
            // resolveWorldPosition()'s own calling convention.
            if (!movementBlockedByWall) {
                const auto& curatedCrcs = curatedCollidableObjectCrcs();
                forEachObject([&](const auto& candidate) {
                    using T = std::decay_t<decltype(candidate)>;
                    if constexpr (std::is_same_v<T, worldmodel::CellObject> ||
                                  std::is_same_v<T, worldmodel::GroupObject>) {
                        return;
                    } else {
                        if (movementBlockedByWall || candidate.isSelf ||
                            curatedCrcs.find(candidate.objectCrc) == curatedCrcs.end()) {
                            return;
                        }
                        auto objPos = resolveWorldPosition(worldSnapshot, candidate);
                        if (!objPos.has_value()) {
                            return; // graceful fallback - unresolved position, can't test it
                        }
                        if (segmentBlockedByCuratedObject(*objPos, preMovePos, predictedSelfPos)) {
                            movementBlockedByWall = true;
                        }
                    }
                });
            }

            if (movementBlockedByWall) {
                if(smoke&&smokePhase==3)++smokeBlockedFrames;
                predictedSelfPos=preMovePos;
                newFloorHeight.reset();floorSource="blocked_hold";floorOwner=0;
                persistentFloorTriangleIndex=oldFloorTriangle;persistentFloorTriangleCellIndex=oldFloorCell;
                persistentFloorTriangleBuildingId=oldFloorBuilding;
            }
            if(proposedCellCrossing&&cellTraversal.commit(*proposedCellCrossing,!movementBlockedByWall)) {
                persistentCellIndex=cellTraversal.cell();
                if(serverCellTracking&&proposedCellParent)predictedSelfParentId=*proposedCellParent;
                journalCell(*proposedCellCrossing,predictedSelfParentId,"committed");
                if(cellSmoke){if(proposedCellCrossing->destinationCell==1)++cellEnterCommits;else if(proposedCellCrossing->destinationCell==0)++cellExitCommits;else throw std::runtime_error("Cell smoke: unexpected room");}
            } else if(proposedCellCrossing) {
                journalCell(*proposedCellCrossing,predictedSelfParentId,"blocked");
            }

            // Real bugfix, found live (round 2): making terrain the
            // universal fallback (see the previous version of this comment)
            // fixed the outdoor-approach clipping, but overcorrected -
            // self standing on an upper floor (e.g. the 3rd story) hits a
            // brief real gap in floor-mesh coverage (crossing between
            // rooms, approaching a stairwell) and got snapped all the way
            // down to GROUND-LEVEL outdoor terrain, confirmed live: "gets
            // to the edge of the cell and then snaps back to the terrain."
            // Terrain is only a sane fallback when genuinely OUTSIDE the
            // building's footprint - indoors, holding the current height
            // unchanged for that one gap frame is far safer than guessing
            // via terrain, which knows nothing about which real floor
            // self is actually standing on. This is now safe to do (it
            // wasn't, the first time this project tried it last night)
            // because two things changed since: the local-to-world Y
            // conversion bug is fixed, and cell 0's own CMSH (real entry
            // ramps/porches) is back in the floor-height search, so the
            // genuinely-outdoors-approaching-the-building case is now
            // usually covered by a real floor hit before ever reaching
            // this fallback at all.
            if (newFloorHeight.has_value()) {
                // Real floor height (Phase 20) - continuously follows the
                // real per-cell collision mesh (real stairs/ramps included),
                // replacing the old frozen-at-last-server-report behavior.
                // newFloorHeight is in the BUILDING-LOCAL frame (see
                // buildingWorldY's own comment above) - convert back to
                // world space before writing to predictedSelfPos.y.
                predictedSelfPos.y = buildingWorldY + *newFloorHeight;
            } else if (terrainSource && !selfInsideAnyBuildingFootprint && !predictedSelfParentId && !movementBlockedByWall) {
                predictedSelfPos.y =
                    terrainSource->querySurfaceHeight(predictedSelfPos.x, predictedSelfPos.z);
                floorSource="terrain";floorOwner=0;
            }
            if(floorSmoke&&smokePhase>=0) {
                float terrainHeight=terrainSource->querySurfaceHeight(predictedSelfPos.x,predictedSelfPos.z);
                float z=predictedSelfPos.z-offlineBuildingOrigin.z;
                if(predictedSelfPos.y<terrainHeight-.002f)throw std::runtime_error("Floor smoke: exterior feet sank below terrain");
                if(smokeFrames<90||smokeFrames>=830) {
                    float error=std::abs(predictedSelfPos.y-terrainHeight);floorMaxStationaryError=std::max(floorMaxStationaryError,error);
                    if(error>.002f)throw std::runtime_error("Floor smoke: retained stationary fixture did not remain on terrain");
                }
                if(z>=18.f&&z<=28.f) {
                    float localY=predictedSelfPos.y-offlineBuildingOrigin.y;
                    // Legacy FLR point query is independent of the new exterior
                    // support solver, and its authored ramp planes have already
                    // passed the CPU plane-intersection audit.
                    auto expected=queryFloorHeight2DFullScan(offlineCells.front().floorMesh,{0,localY,z},localY);
                    if(!expected||expected->y<.49f)throw std::runtime_error("Floor smoke: missing authored ramp oracle");
                    float error=std::abs(localY-expected->y);floorMaxRampError=std::max(floorMaxRampError,error);
                    if(error>.002f)throw std::runtime_error("Floor smoke: renderer feet do not follow authored ramp");
                    ++floorRampCheckFrames;
                }
                if(std::string(floorSource)=="terrain")++floorTerrainFrames;else if(std::string(floorSource)=="floor_mesh")++floorMeshFrames;
                if(floorBuriedCandidates)++floorBuriedFrames;
                floorMinLocalZ=std::min(floorMinLocalZ,z);
                floorMaxStep=std::max(floorMaxStep,std::abs(predictedSelfPos.y-preMovePos.y));
                if(floorMaxStep>.751f)throw std::runtime_error("Floor smoke: unreachable height discontinuity");
            }
            if(cellSmoke&&smokePhase>=0) {
                if(smokeFrames>=170&&smokeFrames<260) {
                    if(predictedSelfParentId!=1001||persistentCellIndex!=1)throw std::runtime_error("Cell smoke: entry parent/cell not stable");++cellEnteredFrames;
                }
                if(smokeFrames>=340&&smokeFrames<430) {
                    if(predictedSelfParentId!=0||persistentCellIndex!=0)throw std::runtime_error("Cell smoke: exit parent/cell not stable");++cellExitedFrames;
                }
                if(smokeFrames>=430&&smokeFrames<460) {
                    if(!proposedCellCrossing||!movementBlockedByWall||predictedSelfParentId||persistentCellIndex!=0||
                        predictedSelfPos.x!=preMovePos.x||predictedSelfPos.y!=preMovePos.y||predictedSelfPos.z!=preMovePos.z||
                        persistentFloorTriangleIndex!=oldFloorTriangle||persistentFloorTriangleCellIndex!=oldFloorCell||persistentFloorTriangleBuildingId!=oldFloorBuilding)
                        throw std::runtime_error("Cell smoke: blocked proposal leaked position, parent, or floor cursor");++cellBlockedFrames;
                }
                clientcommon::MovementFrame frame{selfObjectId,predictedSelfParentId,predictedSelfPos.x,predictedSelfPos.y,predictedSelfPos.z,predictedSelfYawRadians,currentSpeed};
                auto wire=clientcommon::movementWireFrame(frame,clientcommon::MovementParentFrame{offlineBuildingOrigin.x,offlineBuildingOrigin.y,offlineBuildingOrigin.z,worldmodel::rotationHeading(offlineBuildingFrame.rotation),offlineBuildingFrame.rotation});
                if(!wire)throw std::runtime_error("Cell smoke: missing wire frame");
                auto world=wire->parentId?offlineBuildingFrame.toWorld({wire->x,wire->y,wire->z}):assets::Float3{wire->x,wire->y,wire->z};
                DirectX::XMFLOAT3 reconstructed{world.x,world.y,world.z};
                float error=std::hypot(std::hypot(reconstructed.x-predictedSelfPos.x,reconstructed.z-predictedSelfPos.z),reconstructed.y-predictedSelfPos.y);
                cellMaxWireError=std::max(cellMaxWireError,error);if(error>.002f)throw std::runtime_error("Cell smoke: local/world wire mismatch");
            }
            journalFloor(floorSource,floorOwner,floorBuriedCandidates);
            // else: hold predictedSelfPos.y unchanged - the graceful
            // fallback for a real gap in floor-mesh coverage while
            // genuinely indoors (see comment above).

            // Include the final zero-speed position, brief taps and idle synchronization.
            // Indoor reports require a resolved building coordinate frame.
            if(networkThread||cellSmoke) {
                std::optional<clientcommon::MovementParentFrame> parent;
                if(predictedSelfParentId) {
                    auto cell=worldSnapshot.find(predictedSelfParentId);
                    if(cell!=worldSnapshot.end()&&cell->second.containmentMessagesSeen&&cell->second.containerId) {
                        auto building=worldSnapshot.find(cell->second.containerId);
                        if(building!=worldSnapshot.end()&&building->second.transformMessagesSeen) {
                            const auto& b=building->second;
                            if(auto transform=worldmodel::objectLocalTransform(b))parent=clientcommon::MovementParentFrame{b.x,b.y,b.z,worldmodel::rotationHeading(transform->rotation),transform->rotation};
                        }
                    }
                }
                if(movementKeyHeld&&std::abs(predictedSelfPos.x-preMovePos.x)+std::abs(predictedSelfPos.z-preMovePos.z)<1e-6f)currentSpeed=0;
                clientcommon::MovementFrame frame{selfObjectId,predictedSelfParentId,predictedSelfPos.x,predictedSelfPos.y,predictedSelfPos.z,predictedSelfYawRadians,currentSpeed};
                const auto reportPollMs=movementMillis(Clock::now());
                if(auto report=movementReporter.poll(reportPollMs,frame,parent)) {
                    const auto& wire=report->frame;
                    journalFrame(*report,frame,parent);
                    journalMovement("tx_queued",wire.objectId,wire.parentId,report->counter,wire.x,wire.y,wire.z,wire.speed,reportPollMs,report->timestamp);
                    if(networkThread)networkThread->enqueueSend(std::move(report->payload));
                }
            }
        }

        // Publish rendered geometry and its immutable camera BVH together.
        assetWorker.updateSelfPosition(predictedSelfPos.x, predictedSelfPos.z);

        while (auto evictedCoord = assetWorker.terrainEvicted.tryPop()) {
            auto it = terrainChunkHandles.find(*evictedCoord);
            if (it != terrainChunkHandles.end()) {
                gfx.unloadTerrainChunk(it->second);
                terrainChunkHandles.erase(it);
                terrainCameraMeshes.erase(*evictedCoord);
            }
        }
        for (int i = 0; i < kMaxAssetUploadsPerFrame; ++i) {
            auto ready = assetWorker.terrainReady.tryPop();
            if (!ready.has_value()) {
                break;
            }
            auto old=terrainChunkHandles.find(ready->coord);
            if(old!=terrainChunkHandles.end())gfx.unloadTerrainChunk(old->second);
            terrainChunkHandles[ready->coord] = gfx.loadTerrainChunk(ready->meshData);
            terrainCameraMeshes[ready->coord] = std::move(ready->cameraMesh);
        }
        buildingCache.drainReady(assetWorker, gfx, kMaxAssetUploadsPerFrame);

        if((offline||nativeClient)&&offlineTitleReady&&!inspectionMode&&window.takeKeyPress('9')&&!cameraProbeActive) {
            savedProbeCamera=camera;cameraProbeActive=true;cameraProbeStart=now;
            std::cout<<"NATIVE_CAMERA_TEST started (camera only; restores zoom/orbit after 12 seconds)\n";
        }
        float probeSeconds=cameraProbeActive?std::chrono::duration<float>(now-cameraProbeStart).count():0;
        if(cameraProbeActive&&probeSeconds>=12) {
            cameraProbeActive=false;camera=savedProbeCamera;cameraBoom.reset();
            std::cout<<"NATIVE_CAMERA_TEST complete\n";
        }
        cameraProbePhase=cameraSmoke&&smokePhase>=0?smokeFrames/90:cameraProbeActive?int(probeSeconds/4):-1;
        if(cameraProbePhase>=0) {
            camera.yaw=cameraProbePhase==0?0.f:DirectX::XM_PI;
            camera.pitch=cameraProbePhase==0?0.f:cameraProbePhase==1?.4f:-.3f;
            camera.distance=cameraProbePhase==2?8.f:60.f;
        }

        // Phase 18 - inspection mode drives the free FlyCamera instead of
        // the normal self-locked FollowCamera; viewMatrixNow is computed
        // once here and reused by both the main draw pass and the picking
        // ray below, rather than re-branching at each call site.
        if (inspectionMode) {
            if(offlinePlayback)offlinePlayback->advance(deltaSeconds,0);
            if(nativeClient&&livePlayback&&liveStanding)livePlayback->advance(deltaSeconds,0);
            flyCamera.update(window, deltaSeconds);
        } else {
            if(cameraProbePhase<0)screenshotCapture.applyCameraOverride(camera, predictedSelfYawRadians);
            camera.update(window, predictedSelfPos, deltaSeconds);
        }
        DirectX::XMMATRIX viewMatrixNow =
            inspectionMode ? flyCamera.viewMatrix() : camera.viewMatrix(predictedSelfPos);

        if((offline||nativeClient)&&!inspectionMode&&predictedSelfInitialized) {
            assets::Float3 origin{predictedSelfPos.x,predictedSelfPos.y+camera.targetHeightOffset,predictedSelfPos.z};
            assets::Float3 destination{camera.position.x,camera.position.y,camera.position.z};
            assets::Float3 d{destination.x-origin.x,destination.y-origin.y,destination.z-origin.z};
            float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z),fraction=1;
            const float radius=renderer::cameraClearanceRadius(DirectX::XM_PIDIV4,
                float(gfx.renderWidth())/float(gfx.renderHeight()),.1f);
            bool overlap=false;uint32_t triangleTests=0;const char* hitKind="none";
            auto query=[&](const renderer::CameraCollisionMesh& mesh,assets::Float3 from,assets::Float3 to,const char* kind) {
                auto hit=mesh.sweep(from,to,radius);triangleTests+=hit.triangleTests;
                overlap=overlap||hit.initialOverlap;
                if(hit.hit&&hit.fraction<=fraction){fraction=hit.fraction;hitKind=kind;}
            };
            for(const auto& [coord,mesh]:terrainCameraMeshes)if(mesh)query(*mesh,origin,destination,"terrain");
            forEachObject([&](const auto& obj) {
                if(obj.isSelf)return;
                const auto* cells=buildingCache.get(obj.objectCrc,assetWorker);if(!cells||!worldmodel::objectLocalTransform(obj))return;
                // Query every loaded instance; an origin-distance cutoff can miss a large building.
                auto a=worldToBuildingLocal({origin.x,origin.y,origin.z},obj);
                auto b=worldToBuildingLocal({destination.x,destination.y,destination.z},obj);
                for(const auto& cell:*cells)if(cell.cameraMesh)
                    query(*cell.cameraMesh,{a.x,a.y,a.z},{b.x,b.y,b.z},"building");
            });
            float safe=std::max(0.f,length*fraction-(fraction<1?.01f:0.f));
            float resolved=cameraBoom.update(safe,deltaSeconds);
            // If the target itself overlaps, report it rather than moving the character.
            // A tiny eye offset keeps LookAt defined; clearance is not claimed in that case.
            if(length>.0001f){float scale=std::max(.001f,resolved)/length;
                camera.position={origin.x+d.x*scale,origin.y+d.y*scale,origin.z+d.z*scale};}
            if(cameraSmoke&&smokePhase>=0) {
                if(std::string_view(hitKind)=="building")++cameraBuildingFrames;
                if(std::string_view(hitKind)=="terrain")++cameraTerrainFrames;
                if(overlap)++cameraOverlapFrames;
                if(resolved>safe+.0001f)throw std::runtime_error("Camera smoke: unsafe boom release");
            }
            journalCamera(length,safe,resolved,radius,hitKind,overlap,triangleTests,terrainCameraMeshes.size());
        } else cameraBoom.reset();
        if((offline||nativeClient)&&!inspectionMode)viewMatrixNow=camera.viewMatrix(predictedSelfPos);
        const bool gaitProfileCapture=smoke&&!actionSmoke&&!cameraSmoke&&!floorSmoke&&!cellSmoke&&smokePhase==1&&(smokeFrames==12||smokeFrames==28);
        if(gaitProfileCapture) {
            // Change only this draw's view; movement and the animation clock
            // remain untouched. Profile view exposes depth-wise knee flexion.
            auto eye=DirectX::XMVectorSet(predictedSelfPos.x+3.2f*std::cos(predictedSelfYawRadians),
                predictedSelfPos.y+1.0f,predictedSelfPos.z-3.2f*std::sin(predictedSelfYawRadians),1);
            auto target=DirectX::XMVectorSet(predictedSelfPos.x,predictedSelfPos.y+.9f,predictedSelfPos.z,1);
            viewMatrixNow=DirectX::XMMatrixLookAtLH(eye,target,DirectX::XMVectorSet(0,1,0,0));
        }
        std::vector<clientcommon::InspectionTarget> inspectionTargets;
        std::optional<clientcommon::InspectionTarget> inspectionPlayer;dummyclient::ChatCommandContext chatContext;
        clientcommon::TargetPoint inspectionSelf{predictedSelfPos.x,predictedSelfPos.y,predictedSelfPos.z};
        if(targetHud) {
            clientcommon::IndicatorScene indicatorScene;
            forEachObject([&](const auto& obj){
                using T=std::decay_t<decltype(obj)>;
                if constexpr(std::is_same_v<T,worldmodel::CreatureObject>) {
                    clientcommon::IndicatorCreature context;context.pvp=obj.pvpStatusBitmask;
                    context.container=obj.containerId;context.containmentKnown=obj.containmentMessagesSeen!=0;context.objectCrc=obj.objectCrc;
                    if(obj.base3&&obj.base3->statesCurrent){context.link=obj.base3->creatureLinkId;context.options=uint32_t(obj.base3->tangible.optionsBitmask);}
                    indicatorScene.emplace(obj.objectId,context);
                    clientcommon::InspectionTarget target;target.id=obj.objectId;target.status=clientcommon::creatureStatus(obj);
                    if(obj.base3)target.name=obj.base3->tangible.customObjectName;
                    if(obj.isSelf){chatContext.actor=obj.objectId;if(obj.base6)chatContext.mood=obj.base6->moodId;inspectionPlayer=std::move(target);return;}
                    if(obj.transformMessagesSeen)if(auto p=resolveWorldPosition(worldSnapshot,obj)) {
                        target.position={p->x,p->y,p->z};target.resolved=true;
                        if(auto pose=remotePoses.find(obj.objectId);pose!=remotePoses.end())
                            target.position={pose->second.x,pose->second.y,pose->second.z};
                    }
                    inspectionTargets.push_back(std::move(target));
                }
            });
            auto indicators=[&](clientcommon::InspectionTarget& target) {
                target.status.tef=clientcommon::creatureTef(target.id,indicatorScene);
                if(target.status.states&&(*target.status.states&(uint64_t(1)<<27)))
                    target.status.mountVehicle=clientcommon::creatureVehicle(target.id,indicatorScene,[&](uint32_t crc){return meshResolver.templateGameObjectType(crc);});
            };
            if(inspectionPlayer)indicators(*inspectionPlayer);
            for(auto& target:inspectionTargets)indicators(target);
            if(inspectionPlayer)forEachObject([&](const auto& obj){
                using T=std::decay_t<decltype(obj)>;
                if constexpr(std::is_same_v<T,worldmodel::PlayerObject>)
                    if(obj.containmentMessagesSeen&&obj.containerId==inspectionPlayer->id)
                        {clientcommon::setPlayerForce(inspectionPlayer->status,obj);if(obj.base9)chatContext.language=obj.base9->languageId;}
            });
            targetSelection.reconcile(inspectionTargets);
            const bool cycleTarget=window.takeKeyPress(0x09),pickTarget=window.takeKeyPress('Q'),clearTarget=window.takeKeyPress(0x08);
            int targetCursorX=0,targetCursorY=0;
            if(cycleTarget)targetSelection.cycle(inspectionTargets,inspectionSelf);
            if(pickTarget) {
                window.cursorPosition(targetCursorX,targetCursorY);DirectX::XMFLOAT3 o,d;
                auto projection=DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4,
                    float(gfx.renderWidth())/gfx.renderHeight(),.1f,8192.f);
                screenPointToWorldRay(targetCursorX,targetCursorY,window.width(),window.height(),viewMatrixNow,projection,o,d);
                targetSelection.pick(inspectionTargets,inspectionSelf,{o.x,o.y,o.z},{d.x,d.y,d.z});
            }
            if(clearTarget)targetSelection.clear();
            const auto* selected=targetSelection.selected(inspectionTargets);
            targetHud->prepare(selected,inspectionPlayer?&*inspectionPlayer:nullptr);
            if(nativeClient&&statusJournal&&statusLogEvents<512) {
                std::ostringstream record;record<<std::setprecision(9)<<"\"selected_id\":"<<targetSelection.id();
                if(inspectionPlayer) {
                    const auto& own=inspectionPlayer->status;
                    record<<",\"player_posture\":";if(own.posture)record<<int(*own.posture);else record<<"null";
                    record<<",\"player_states\":";if(own.states)record<<*own.states;else record<<"null";
                    record<<",\"player_tef\":";if(own.tef)record<<(*own.tef?"true":"false");else record<<"null";
                    record<<",\"player_vehicle\":";if(own.mountVehicle)record<<(*own.mountVehicle?"true":"false");else record<<"null";
                    record<<",\"player_id\":"<<own.id<<",\"force_known\":"<<(own.forceKnown?"true":"false")<<",\"force\":";
                    if(own.force)record<<"["<<own.force->current<<","<<own.force->maximum<<"]";else record<<"null";
                    record<<",\"player_ham\":[";
                    for(size_t i=0;i<3;++i){if(i)record<<",";if(own.ham[i])record<<"["<<own.ham[i]->current<<","<<own.ham[i]->maximum<<","<<own.ham[i]->wounds<<"]";else record<<"null";}
                    record<<"]";
                }
                if(selected) {
                    const auto& status=selected->status;
                    record<<",\"posture\":";if(status.posture)record<<int(*status.posture);else record<<"null";
                    record<<",\"states\":";if(status.states)record<<*status.states;else record<<"null";
                    record<<",\"tef\":";if(status.tef)record<<(*status.tef?"true":"false");else record<<"null";
                    record<<",\"vehicle\":";if(status.mountVehicle)record<<(*status.mountVehicle?"true":"false");else record<<"null";
                    record<<",\"ham_counter\":"<<status.hamCounter<<",\"max_counter\":"<<status.maxHamCounter<<",\"wounds_counter\":"<<status.woundsCounter<<",\"ham\":[";
                    for(size_t i=0;i<3;++i) {if(i)record<<",";if(auto bar=status.ham[i])record<<"{\"current\":"<<bar->current<<",\"maximum\":"<<bar->maximum<<",\"wounds\":"<<bar->wounds<<",\"current_fraction\":"<<bar->currentFraction()<<",\"usable_fraction\":"<<bar->usableFraction()<<"}";else record<<"null";}
                    record<<"]";
                }
                if(record.str()!=lastStatusRecord) {lastStatusRecord=record.str();++statusLogEvents;statusJournal<<"{\"elapsed_ms\":"<<movementMillis(std::chrono::steady_clock::now())<<","<<lastStatusRecord<<"}\n";statusJournal.flush();}
            }
            if(nativeClient&&targetLogEvents<256&&(cycleTarget||pickTarget||clearTarget||targetLoggedId!=targetSelection.id()||targetLoggedResolved!=(selected&&selected->resolved))) {
                targetLoggedId=targetSelection.id();targetLoggedResolved=selected&&selected->resolved;++targetLogEvents;
                std::cout<<"NATIVE_TARGET selected_id="<<targetLoggedId<<" resolved="<<targetLoggedResolved<<"\n";
                if(targetJournal) {
                    targetJournal<<"{\"event\":\""<<(clearTarget?"clear":pickTarget?"pick":cycleTarget?"cycle":"reconcile")
                        <<"\",\"elapsed_ms\":"<<movementMillis(std::chrono::steady_clock::now())
                        <<",\"selected_id\":"<<targetLoggedId<<",\"resolved\":"<<(targetLoggedResolved?"true":"false");
                    if(pickTarget)targetJournal<<",\"cursor_pixels\":["<<targetCursorX<<","<<targetCursorY<<"],\"viewport_pixels\":["<<window.width()<<","<<window.height()<<"]";
                    if(selected)if(auto distance=clientcommon::TargetSelection::distance(*selected,inspectionSelf))targetJournal<<",\"distance_m\":"<<*distance;
                    targetJournal<<"}\n";targetJournal.flush();
                }
            }
        }
        gfx.beginFrame(offline?.30f:.05f,offline?.47f:.05f,offline?.65f:.08f);

        float aspect = static_cast<float>(gfx.renderWidth()) / static_cast<float>(gfx.renderHeight());
        DirectX::XMMATRIX projection =
            DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, aspect, 0.1f, 8192.0f);
        gfx.setViewProjection(viewMatrixNow, projection);

        // Real portal-based cell visibility (Phase 22) - one world-space
        // frustum built once per frame, reused for every building's
        // computeVisibleCells() call in the draw pass below (row-vector
        // convention: combined = view * projection, matching how
        // gfx.setViewProjection() itself composes them).
        renderer::Frustum cameraFrustum = renderer::Frustum::fromViewProjection(
            DirectX::XMMatrixMultiply(viewMatrixNow, projection));

        // Real terrain (Phase 8, relocated to a background thread in Phase
        // 14) - tells the asset worker thread where self is (cheap atomic
        // store; the actual generation happens over there, off this
        // thread), then drains whatever it's produced since last frame.
        // Evictions first (unbounded - just a GPU buffer destroy, cheap),
        // then new chunks (budgeted via kMaxAssetUploadsPerFrame, same
        // streaming-budget reasoning as meshCache.drainReady() below).
        // Every currently-tracked chunk is drawn unconditionally each
        // frame regardless of upload status - drawTerrainChunk() already
        // skips one whose GPU upload hasn't completed yet (Phase 14 step
        // 2), so there's no need to track readiness twice.
        for (const auto& [coord, handle] : terrainChunkHandles) {
            gfx.drawTerrainChunk(handle);
        }

        // Real-mesh resolution (Phase 14: CPU parsing happens on
        // assetWorker's thread now - see MeshHandleCache's own comment)
        // drained before this frame's draw pass so a mesh that just became
        // ready can appear the same frame it completes, not one frame late.
        materialFixtureTexturedDraws=0;
        meshCache.drainReady(assetWorker, gfx, kMaxAssetUploadsPerFrame);
        skeletalMeshCache.drainReady(assetWorker, gfx, kMaxAssetUploadsPerFrame);


        // Phase 17 Step 5 - real cursor-based picking. Computed once per
        // frame (cheap) regardless of whether anything's actually
        // interactable; the draw pass below tests every drawn object's
        // already-computed world position against this same ray, so
        // there's no separate whole-scene scan.
        DirectX::XMFLOAT3 pickRayOrigin{};
        DirectX::XMFLOAT3 pickRayDir{};
        {
            int cursorX = 0;
            int cursorY = 0;
            window.cursorPosition(cursorX, cursorY);
            screenPointToWorldRay(cursorX, cursorY, window.width(), window.height(), viewMatrixNow,
                                   projection, pickRayOrigin, pickRayDir);
        }
        constexpr float kInteractionPickRadius = 1.25f;   // world units, tuned by eye
        constexpr float kMaxInteractionDistance = 12.0f;  // world units, tuned by eye
        uint64_t hoveredObjectId = 0;
        uint32_t hoveredObjectCrc = 0;
        DirectX::XMFLOAT3 hoveredObjectPos{};
        float hoveredDistance = kMaxInteractionDistance;

        // A bounded 600-frame trace of one nearby moving creature records
        // the actual presentation coordinates, alongside raw server events.
        if(nativeClient&&!remotePresentationProbeId) {
            float nearest=50*50;
            for(const auto& [id,pose]:remotePoses) {
                const auto object=worldSnapshot.find(id);
                if(object==worldSnapshot.end()||object->second.speed<=0)continue;
                const float dx=pose.x-predictedSelfPos.x,dz=pose.z-predictedSelfPos.z;
                if(dx*dx+dz*dz<nearest) {nearest=dx*dx+dz*dz;remotePresentationProbeId=id;}
            }
        }

        // Real terrain grading (see gradedBuildingIds' own comment) - which
        // already-graded buildings are still actually present this frame,
        // used for the post-scan removal diff right after this forEach call.
        std::unordered_set<uint64_t> seenBuildingIdsThisFrame;

        forEachObject([&](const auto& obj) {
            using T = std::decay_t<decltype(obj)>;
            if constexpr (std::is_same_v<T, worldmodel::CellObject> ||
                          std::is_same_v<T, worldmodel::GroupObject>) {
                return; // containers/session objects, not spatial things to draw
            } else {
                if (obj.transformMessagesSeen == 0) {
                    return;
                }
                // A non-self object whose containment chain isn't fully
                // resolved yet is skipped entirely THIS frame rather than
                // drawn at a guessed position - see resolveWorldPosition()'s
                // own comment for the real bug this replaced (silently
                // drawing at a nonsensical near-world-origin fallback).
                DirectX::XMFLOAT3 objPos;
                if (obj.isSelf) {
                    objPos = predictedSelfPos;
                } else {
                    auto resolved = resolveWorldPosition(worldSnapshot, obj);
                    if (!resolved.has_value()) {
                        return;
                    }
                    objPos = *resolved;
                }
                auto objectPose=worldmodel::resolveWorldTransform(worldSnapshot,obj);
                if(!objectPose)return;
                auto objectModel=spatialModelMatrix(*objectPose);
                DirectX::XMFLOAT3 halfExtents = visualizerBoxHalfExtentsFor(obj.typeTag);
                DirectX::XMFLOAT4 color = visualizerColorFor(obj.typeTag, obj.isSelf);
                // direction is scaled 0-100 for a full turn, not degrees
                // (see UpdateTransformMessage.h's own field comment). Self
                // is drawn at the LOCALLY predicted position/facing (Phase
                // 13, player movement), not the last server-reported obj.x/
                // y/z/direction, so self's own rendered body stays in sync
                // with the camera/grid/terrain (all of which also follow
                // predictedSelfPos) rather than lagging a step behind.
                float yawRadians = obj.isSelf
                                        ? predictedSelfYawRadians
                                        : worldmodel::rotationHeading(objectPose->rotation);

                if(auto presented=remotePoses.find(obj.objectId);presented!=remotePoses.end()) {
                    objPos={presented->second.x,presented->second.y,presented->second.z};
                    yawRadians=presented->second.yaw;
                    if(obj.objectId==remotePresentationProbeId&&remotePresentedSamples<600) {
                        const auto& source=worldSnapshot.at(obj.objectId);
                        journalMovement("remote_presented",obj.objectId,source.parentId,source.movementCounter,
                            objPos.x,objPos.y,objPos.z,source.speed);
                        ++remotePresentedSamples;
                    }
                }

                // Phase 17 Step 5 - real cursor picking, piggybacked on this
                // same per-object pass rather than a second whole-scene
                // scan. Self can't interact with itself. Passing the
                // running `hoveredDistance` in as this call's own
                // maxDistance means each closer hit naturally tightens the
                // search, so the loop converges on the nearest interactable
                // under the cursor without a separate sort/compare step.
                if (!obj.isSelf) {
                    if (auto hitDistance = raySphereIntersect(
                            pickRayOrigin, pickRayDir, objPos, kInteractionPickRadius,
                            hoveredDistance)) {
                        hoveredObjectId = obj.objectId;
                        hoveredObjectCrc = obj.objectCrc;
                        hoveredObjectPos = objPos;
                        hoveredDistance = *hitDistance;
                    }
                }

                // Real mesh geometry (2026-07-18), where resolution
                // succeeds - see RealMeshResolver. A real mesh's own
                // vertices are already positioned relative to the object's
                // natural pivot (unlike the placeholder box primitive,
                // which is symmetric around its center and needs the
                // ground-level adjustment below), so it's drawn directly at
                // objPos with no offset.
                //
                // Real SKELETAL mesh geometry (Phase 15) - creature/player
                // objects only, since RealMeshResolver's own candidate list
                // excludes them entirely (querying it for them would only
                // ever return "not found"). Bind-pose only this phase (no
                // animation/skinning yet - see SkeletalMesh.h), drawn the
                // same way a static mesh is, just once per real submesh.
                //
                // Real BUILDING geometry (Phase 16) - tried for every
                // non-creature/player object before falling back to the
                // ordinary static-mesh path, since real buildings ("Eese's
                // House") decode as plain TangibleObject just like any
                // other item - there's no cheap typeTag signal to tell them
                // apart in advance, only the template data itself
                // (RealBuildingResolver's own empty-appearanceFilename
                // check) reveals it. A non-building object's one-time
                // buildingCache lookup fails and caches "not a building"
                // forever (same cache-forever-even-failures convention
                // every resolver here already uses), so this costs at most
                // one wasted worker-thread resolve attempt per distinct
                // real object ever seen, not a per-frame cost. All cells
                // (exterior shell + every interior room) draw at the
                // building's own resolved position - cell-relative
                // placement isn't implemented yet (Phase 16 explicitly
                // scoped to rendering, not cell-relative movement).
                const std::vector<renderer::MeshHandle>* realMesh = nullptr;
                const std::vector<renderer::MeshHandle>* skeletalSubmeshes = nullptr;
                const std::vector<CellHandles>* buildingCells = nullptr;
                bool drewAnimatedSelf = false;
                if (obj.typeTag == worldmodel::ObjectTypeTag::Creature ||
                    obj.typeTag == worldmodel::ObjectTypeTag::Player) {
                    // Phase 21 (animation), v1 self-only - resolved once,
                    // synchronously, the first time self's real objectCrc is
                    // seen here (see SelfAnimationData's own comment on the
                    // deliberate self-only scope cut - extending this to
                    // other visible creatures/players is a real, natural
                    // follow-up: give them the same treatment
                    // AssetWorkerThread already gives skeletalMeshCache,
                    // just async instead of this one-shot synchronous
                    // call). A resolution failure (no real skeleton/mesh
                    // data for this template) permanently falls back to the
                    // existing bind-pose skeletalMeshCache path for self
                    // too - same graceful degradation as every other
                    // resolver here.
                    if (obj.isSelf && !selfAnimResolveAttempted) {
                        selfAnimResolveAttempted = true;
                        selfAnimData = skeletalMeshResolver.resolveSelfAnimationData(obj.objectCrc);
                        if (selfAnimData.has_value() && !selfAnimData->meshParts.empty()) {
                            if(nativeClient) {
                                try {
                                    configureAuthoredPlayback(livePlayback,liveClipPaths,liveActionNames,liveUnsupportedActions,
                                        *selfAnimData,meshResolver,skeletalMeshResolver,liveAmbientEligible);
                                    std::cout<<"NATIVE_ANIMATION_READY supported_actions="<<liveActionNames.size()<<"\n";
                                }catch(const std::exception& e) {
                                    livePlayback.reset();std::cerr<<"Native standing controller unavailable: "<<e.what()<<"\n";
                                }
                            }
                            selfMeshBoneBindings.clear();
                            selfDynamicMeshes.clear();
                            for (size_t pi = 0; pi < selfAnimData->meshParts.size(); ++pi) {
                                const auto& part = selfAnimData->meshParts[pi];
                                auto binding =
                                    animation::bindMeshBoneIndices(selfAnimData->skeleton, part.boneNames);
                                // One-time diagnostics (fire once per
                                // session) - see AnimationDiagnosticLogging's
                                // own comments.
                                dummyclient::logSkeletonToMeshBoneBinding(selfAnimData->skeleton, binding, pi);
                                selfMeshBoneBindings.push_back(std::move(binding));
                                for (const auto& submesh : part.submeshes) {
                                    auto handle=gfx.allocateDynamicMesh(submesh.positions.size(),submesh.indices.size());
                                    auto texture=resolveSubmeshTexture(submesh.shaderFilename,[&](const std::string& name){return meshResolver.extract(name);});
                                    if(texture)handle.textureDescriptorSet=gfx.loadTexture(*texture).descriptorSet;
                                    else if(offline)throw std::runtime_error("Animated material failed: "+submesh.shaderFilename);
                                    selfDynamicMeshes.push_back(std::move(handle));
                                }
                                dummyclient::logVertexWeightSample(pi, part.boneNames.size(), part.vertexWeights);
                            }
                        } else {
                            selfAnimData.reset();
                        }
                    }

                    if (obj.isSelf && selfAnimData.has_value() && !selfAnimData->meshParts.empty()) {
                        // Real posture/mood/weapon/combat-state context -
                        // see creatureanim::CreatureAnimationContext's own
                        // comment. Falls back to the struct's own defaults
                        // (posture=0 etc.) for any self-typed object that
                        // isn't a real CreatureObject.
                        creatureanim::CreatureAnimationContext animContext;
                        if constexpr (std::is_same_v<T, worldmodel::CreatureObject>) {
                            animContext = creatureanim::resolveAnimationContext(obj, selfIsMoving);
                        } else {
                            animContext.isMoving = selfIsMoving;
                        }
                        const char* stateName = offline?"loop_standing":creatureanim::stateNameFor(animContext);

                        if (!selfAnimData->stateTable.states.empty()) {
                            auto cacheIt = selfClipCache.find(stateName);
                            if (cacheIt == selfClipCache.end()) {
                                assets::AnimationSelectionContext selectionContext =
                                    creatureanim::toSelectionContext(animContext, selfAnimData->gender);
                                std::string clipPath = selfClipPathForState(
                                    selfAnimData->stateTable, stateName, selectionContext, selfIsMoving);
                                std::cout << "[ANIMDBG] resolved clip for state=" << stateName
                                           << " (posture=" << static_cast<int>(animContext.posture)
                                           << " mood=" << animContext.moodString
                                           << " weaponId=" << animContext.weaponId
                                           << " stateBitmask=" << creatureanim::describeStateBitmask(
                                                                       animContext.stateBitmask)
                                           << "): " << clipPath << "\n";
                                std::optional<assets::AnimationClipData> clip;
                                if (!clipPath.empty()) {
                                    clip = skeletalMeshResolver.resolveAnimationClip(clipPath);
                                }
                                cacheIt = selfClipCache.emplace(stateName, std::move(clip)).first;
                            }
                            const assets::AnimationClipData* clip =
                                (cacheIt->second.has_value() && !animControls.forceBindPoseOnly)
                                    ? &cacheIt->second.value()
                                    : nullptr;
                            // Offline walking consumed 0 -> dt in the movement
                            // step, so draw that same endpoint on entry. Preserve
                            // the clip's INFO frame rate and common loop duration.
                            if(offlinePlayback) {
                                selfActiveAnimationState=stateName;
                                selfAnimFrame=offlinePlayback->frame(1);
                            } else if(selfActiveAnimationState!=stateName) {
                                selfActiveAnimationState=stateName;
                                selfAnimFrame=(offline&&selfIsMoving&&clip)?clip->loopFrame(deltaSeconds*clip->framesPerSecond):0;
                            } else if(clip) {
                                selfAnimFrame=clip->loopFrame(selfAnimFrame+deltaSeconds*clip->framesPerSecond);
                            }

                            std::vector<int> clipBoneIndices;
                            if (clip != nullptr) {
                                clipBoneIndices =
                                    animation::bindClipBoneIndices(selfAnimData->skeleton, *clip);
                            }
                            auto localTransforms = animation::sampleLocalBoneTransforms(
                                selfAnimData->skeleton, clip, clipBoneIndices, selfAnimFrame,
                                animControls.rotationCompositionVariant, animControls.axisFixVariant,
                                animControls.disableAnimTranslation, &animControls.isolateBoneNames,
                                animControls.fingerCompositionVariant, animControls.fingerAxisFixVariant,
                                animControls.useRealBindPoseFormula,
                                animControls.bindRotationAxisFixVariant);
                            if(offlinePlayback)localTransforms=offlinePlayback->sample(selfAnimData->skeleton);
                            else if(nativeClient&&livePlayback&&liveStanding)localTransforms=livePlayback->sample(selfAnimData->skeleton);
                            if(smoke&&!actionSmoke&&!cameraSmoke&&!floorSmoke&&!cellSmoke&&((smokePhase==0&&smokeFrames==15)||(smokePhase==1&&smokeFrames<8))) {
                                TransitionSample trace{smokePhase==0?0.f:(smokeFrames+1)/30.f,
                                    offlinePlayback->idleFrame(),offlinePlayback->frame(1),offlinePlayback->movingWeight(),{}};
                                for(const auto& matrix:localTransforms) {
                                    DirectX::XMFLOAT4X4 m;DirectX::XMStoreFloat4x4(&m,matrix);trace.localMatrices.push_back(m);
                                }
                                smokeTransitionSamples.push_back(std::move(trace));
                            }
                            if(smoke&&smokePhase==1&&smokeFrames==7) {
                                smokeWalkStart=predictedSelfPos;smokeWalkStartFrame=selfAnimFrame;
                            }
                            auto worldTransforms =
                                animation::computeWorldBoneTransforms(selfAnimData->skeleton, localTransforms);
                            if(smoke&&smokePhase==1&&smokeFrames>=8) {
                                auto origin=[&](const char* name) {
                                    for(size_t i=0;i<selfAnimData->skeleton.bones.size();++i) {
                                        const auto& boneName=selfAnimData->skeleton.bones[i].name;
                                        if(boneName.size()==std::strlen(name)&&std::equal(boneName.begin(),boneName.end(),name,
                                            [](unsigned char a,unsigned char b){return std::tolower(a)==b;})) {
                                            DirectX::XMFLOAT4X4 matrix;
                                            DirectX::XMStoreFloat4x4(&matrix,worldTransforms[i]);
                                            return DirectX::XMVectorSet(matrix._41,matrix._42,matrix._43,0);
                                        }
                                    }
                                    throw std::runtime_error("Gait diagnostic missing leg joint");
                                };
                                auto bend=[&](const char* hip,const char* knee,const char* ankle) {
                                    auto a=DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(origin(knee),origin(hip)));
                                    auto b=DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(origin(ankle),origin(knee)));
                                    float cosine=DirectX::XMVectorGetX(DirectX::XMVector3Dot(a,b));
                                    return std::acos(std::clamp(cosine,-1.0f,1.0f))*180.0f/DirectX::XM_PI;
                                };
                                smokeKneeSamples.push_back({selfAnimFrame,bend("lthigh","lshin","lankle"),bend("rthigh","rshin","rankle")});
                                DirectX::XMFLOAT3 pelvis;
                                DirectX::XMStoreFloat3(&pelvis,origin("root"));
                                smokeHipSamples.push_back({selfAnimFrame,pelvis.x,pelvis.y,pelvis.z,
                                    predictedSelfPos.x,predictedSelfPos.y,predictedSelfPos.z});
                            }

                            // Real, numbers-only leg-motion sanity check -
                            // see AnimationDiagnosticLogging's own comment.
                            dummyclient::logLegSanityIfMoving(selfAnimData->skeleton, worldTransforms,
                                                               selfAnimFrame, selfIsMoving);

                            // Throttled named-bone state dump - see
                            // AnimationDiagnosticLogging's own comment.
                            dummyclient::logNamedBoneDump(selfAnimData->skeleton, worldTransforms,
                                                           localTransforms, clip, clipBoneIndices,
                                                           selfAnimFrame);

                            // Phase 21 diagnostic - throttle covers the
                            // WHOLE per-part/per-submesh loop below (checked
                            // once per frame here, not once per submesh),
                            // so every body part gets logged together on the
                            // same throttled frame instead of only the
                            // first part encountered.
                            static float lastVertLogTime = -1000.0f;
                            // Threshold is in selfAnimFrame' own units,
                            // which run at 30x real time (see its own
                            // `+= deltaSeconds * 30.0f` update) - 15.0f here
                            // is 0.5 REAL seconds, not 0.5 of this scaled
                            // clock (an earlier version compared directly
                            // against 0.5f, which is 0.5/30 = ~17ms of real
                            // time - effectively unthrottled, and the real
                            // cause of the console-spam complaint).
                            bool shouldLogVertsThisFrame = selfAnimFrame - lastVertLogTime > 15.0f;
                            if (shouldLogVertsThisFrame) {
                                lastVertLogTime = selfAnimFrame;
                            }

                            size_t dynamicMeshIndex = 0;
                            for (size_t partIndex = 0; partIndex < selfAnimData->meshParts.size();
                                 ++partIndex) {
                                const auto& part = selfAnimData->meshParts[partIndex];
                                const auto& meshBoneIndices = selfMeshBoneBindings[partIndex];
                                for (const auto& submesh : part.submeshes) {
                                    if (dynamicMeshIndex >= selfDynamicMeshes.size()) break;
                                    std::vector<assets::Float3> skinnedPositions;
                                    std::vector<assets::Float3> skinnedNormals;
                                    animation::skinSubmeshVertices(
                                        submesh, part.vertexWeights, selfAnimData->skeleton,
                                        meshBoneIndices, part.boneNames, worldTransforms,
                                        skinnedPositions, skinnedNormals,
                                        animControls.useRealBindPoseFormula,
                                        animControls.bindRotationAxisFixVariant);
                                    // Real per-submesh skinned-mesh
                                    // diagnostics - see
                                    // AnimationDiagnosticLogging's own
                                    // comment.
                                    dummyclient::logSkinnedSubmeshDiagnostics(
                                        partIndex, submesh, part.vertexWeights, part.boneNames,
                                        meshBoneIndices, selfAnimData->skeleton, skinnedPositions,
                                        selfAnimFrame, shouldLogVertsThisFrame);
                                    assets::MeshData meshData;
                                    meshData.positions = std::move(skinnedPositions);
                                    meshData.normals = std::move(skinnedNormals);
                                    meshData.uv0 = submesh.uv0;
                                    meshData.indices = submesh.indices;
                                    // Real GPU-handoff dump (capped
                                    // internally) - see
                                    // AnimationDiagnosticLogging's own
                                    // comment.
                                    dummyclient::logGpuHandoffIfBudgetRemains(
                                        partIndex, dynamicMeshIndex, objPos, yawRadians, meshData,
                                        selfAnimFrame);
                                    gfx.updateDynamicMesh(selfDynamicMeshes[dynamicMeshIndex], meshData);
                                    gfx.drawDynamicMesh(selfDynamicMeshes[dynamicMeshIndex], objPos,
                                                         yawRadians, selfDynamicMeshes[dynamicMeshIndex].textureDescriptorSet!=VK_NULL_HANDLE?DirectX::XMFLOAT4{1,1,1,1}:color);
                                    ++dynamicMeshIndex;
                                }
                            }
                        }
                        drewAnimatedSelf = true;
                    }

                    if (!drewAnimatedSelf) {
                        skeletalSubmeshes = skeletalMeshCache.get(obj.objectCrc, assetWorker);
                    }
                } else {
                    buildingCells = buildingCache.get(obj.objectCrc, assetWorker);
                    if (buildingCells == nullptr) {
                        realMesh = meshCache.get(obj.objectCrc, assetWorker);
                    } else {
                        // Real terrain grading - register once per real
                        // building instance (objectId), the first frame its
                        // cell layout successfully resolves; already-graded
                        // instances just get marked seen for the removal
                        // diff below.
                        seenBuildingIdsThisFrame.insert(obj.objectId);
                        if (gradedBuildingIds.insert(obj.objectId).second) {
                            assetWorker.requestGrading(obj.objectId, obj.objectCrc, objPos.x, objPos.z);
                        }
                    }
                }

                if (drewAnimatedSelf) {
                    // Already drawn above via the CPU-skinned dynamic-mesh
                    // path - nothing left to do for this object.
                } else if (skeletalSubmeshes != nullptr) {
                    for (const auto& submeshHandle : *skeletalSubmeshes) {
                        gfx.drawMesh(submeshHandle, objPos, yawRadians, colorForSubmesh(submeshHandle, color));
                        if(materialSmoke&&obj.objectId>=300&&obj.objectId<302&&submeshHandle.textureDescriptorSet!=VK_NULL_HANDLE)
                            ++materialFixtureTexturedDraws;
                    }
                } else if (buildingCells != nullptr) {
                    // Phase 17 fix: a real, live-caught visual bug - drawing
                    // EVERY one of a building's real cells unconditionally,
                    // every frame, regardless of where self actually is,
                    // meant standing inside any one room showed the
                    // overlapping outlines of all 17 OTHER rooms too (each
                    // individually correctly positioned, per real .pob
                    // format research - no missing per-cell transform - but
                    // never meant to all be visible at once from a single
                    // vantage point, since the real game only shows the
                    // room you're actually in via portal-based visibility).
                    // Cell 0 (the exterior shell) always draws - it's the
                    // only cell meant to be seen from outside the building
                    // at all.
                    //
                    // Phase 19 fix: which OTHER cell counts as "self's
                    // current cell" used to come from the server-reported
                    // cellNumber (via predictedSelfParentId's own CellObject)
                    // - a real, live-caught gap found this session showed
                    // that value only ever updates right at zone-in or after
                    // a genuine server-driven relocation (an elevator ride);
                    // ordinary walking between adjacent rooms produces no
                    // such update at all, so normal mode kept rendering
                    // whatever room self was in at zone-in forever, no
                    // matter where they actually walked. Fixed by computing
                    // it locally instead: self's own already-known world
                    // position, transformed into THIS specific building
                    // instance's local mesh space, tested against each real
                    // cell's own real bounds (see CellBounds/
                    // findContainingCellIndex) - entirely independent of any
                    // server round-trip, so it stays correct through
                    // ordinary walking, not just elevator rides.
                    //
                    // Phase 18: inspection mode deliberately bypasses this
                    // filter - every cell draws at once (a "dollhouse
                    // cutaway" view for visual/scale assessment), matching
                    // the free noclip camera also active in this mode. The
                    // gameplay-accurate filter below is unchanged in spirit
                    // for normal play - just upgraded (Phase 22) from a
                    // binary "exterior + self's current cell only" gate to
                    // real portal-based visibility.
                    std::optional<size_t> selfCellIndexHere;
                    if (!inspectionMode) {
                        DirectX::XMFLOAT3 selfLocalHere = worldToBuildingLocal(predictedSelfPos, obj);
                        if(persistentCellBuildingId==obj.objectId&&persistentCellIndex&&*persistentCellIndex<buildingCells->size())selfCellIndexHere=persistentCellIndex;
                        else selfCellIndexHere = findContainingCellIndex(*buildingCells, selfLocalHere);
                    }

                    // Real portal-based cell visibility (Phase 22) - starts
                    // the portal graph walk from self's own current cell if
                    // self is inside THIS building, or cell 0 (exterior)
                    // otherwise - correctly handles both "a room becomes
                    // visible through an open door from outside" and "the
                    // exterior/another room becomes visible through an open
                    // door from inside" as the same graph. Cell 0 is always
                    // additionally forced visible, matching this project's
                    // prior "exterior always draws" behavior - the
                    // building's own ground-level shape reference shouldn't
                    // disappear just because no portal chain currently
                    // reaches it.
                    std::vector<bool> visibleCells;
                    if (!inspectionMode && !buildingCells->empty()) {
                        std::vector<std::vector<assets::CellPortal>> cellPortals;
                        cellPortals.reserve(buildingCells->size());
                        for (const auto& resolvedCell : *buildingCells) {
                            cellPortals.push_back(resolvedCell.portals);
                        }
                        size_t startCellIndex = selfCellIndexHere.value_or(0);
                        visibleCells = worldmodel::computeVisibleCells(
                            cellPortals, (*buildingCells)[0].portalShapes, startCellIndex,
                            [&](assets::Float3 localCenter, float radius) {
                                DirectX::XMFLOAT3 worldCenter = buildingLocalToWorld(
                                    DirectX::XMFLOAT3{localCenter.x, localCenter.y, localCenter.z}, obj);
                                return cameraFrustum.intersectsSphere(worldCenter, radius);
                            });
                        if (!visibleCells.empty()) {
                            visibleCells[0] = true;
                        }
                    }

                    if(frameSmoke&&obj.objectId==offlineBuildingId&&smokePhase>=0) {
                        if(selfCellIndexHere!=persistentCellIndex)throw std::runtime_error("Frame smoke: rendered cell differs from accepted traversal cell");
                        DirectX::XMFLOAT3 modelPoint;DirectX::XMStoreFloat3(&modelPoint,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(cellDoorCenter.x,cellDoorCenter.y,cellDoorCenter.z,1),DirectX::XMLoadFloat4x4(&objectModel)));
                        auto expected=offlineBuildingFrame.toWorld({cellDoorCenter.x,cellDoorCenter.y,cellDoorCenter.z});
                        float error=std::hypot(std::hypot(expected.x-modelPoint.x,expected.z-modelPoint.z),expected.y-modelPoint.y);frameMaxModelError=std::max(frameMaxModelError,error);++frameModelChecks;
                        if(error>.002f)throw std::runtime_error("Frame smoke: GPU model frame disagrees with doorway frame");
                    }
                    for (size_t cellIndex = 0; cellIndex < buildingCells->size(); ++cellIndex) {
                        bool isVisible = cellIndex < visibleCells.size() && visibleCells[cellIndex];
                        if (!inspectionMode && !isVisible) {
                            continue;
                        }
                        for (const auto& submeshHandle : (*buildingCells)[cellIndex].submeshes) {
                            gfx.drawMesh(submeshHandle, objectModel,
                                         colorForSubmesh(submeshHandle, color));
                        }
                    }
                } else if (realMesh != nullptr) {
                    for (const auto& submeshHandle : *realMesh) {
                        gfx.drawMesh(submeshHandle, objectModel,
                                     colorForSubmesh(submeshHandle, color));
                    }
                } else {
                    // FIXED 2026-07-18: objPos is the object's ground/feet
                    // position (confirmed by the ground-grid work above), not
                    // its vertical center - drawWireBox()'s box mesh is
                    // symmetric around `center`, so the box needs to be shifted
                    // up by its own half-height to sit ON TOP of the ground
                    // instead of straddling it.
                    DirectX::XMFLOAT3 center{objPos.x, objPos.y + halfExtents.y, objPos.z};
                    gfx.drawWireBox(center, halfExtents, yawRadians, color);
                }

                // Self's own box (cyan, human-sized) is easy to lose among
                // similarly-colored/sized nearby objects, even with the
                // camera now locked to self and a name label available (see
                // below) - keeping this as a second, always-on cue costs
                // nothing extra to render.
                if (obj.isSelf&&!offline) {
                    DirectX::XMFLOAT3 beaconCenter{objPos.x, objPos.y + 2.0f * halfExtents.y + 4.0f,
                                                    objPos.z};
                    DirectX::XMFLOAT3 beaconHalfExtents{0.15f, 4.0f, 0.15f};
                    DirectX::XMFLOAT4 beaconColor{1.0f, 0.2f, 1.0f, 1.0f}; // magenta
                    gfx.drawWireBox(beaconCenter, beaconHalfExtents, 0.0f, beaconColor);
                }
            }
        });

        // Real terrain grading removal - a previously-graded building no
        // longer seen this frame (real structure destroyed) gets its
        // grading un-registered. Diffed against gradedBuildingIds rather
        // than hooked directly into SceneDestroyObject's handler (main.cpp,
        // outside this function's scope) - mirrors AssetWorkerThread's own
        // terrain-chunk eviction diffing (knownTerrainCoords_ vs.
        // loadedChunks()) just above.
        for (auto it = gradedBuildingIds.begin(); it != gradedBuildingIds.end();) {
            if (seenBuildingIdsThisFrame.find(*it) == seenBuildingIdsThisFrame.end()) {
                assetWorker.requestUngrading(*it);
                it = gradedBuildingIds.erase(it);
            } else {
                ++it;
            }
        }

        // Name labels - a second pass, after every box this frame, since
        // labels use a different shader/blend/topology than
        // drawWireBox()'s (see D3D11Renderer::beginLabelPass()'s own
        // comment on why it doesn't restore box-drawing state itself;
        // beginFrame() re-establishes it fresh next frame regardless).
        // Billboarded to always face the camera, using the camera's own
        // look direction to derive a world-space right/up basis - the
        // renderer itself has no notion of "camera", only the view/
        // projection matrices already bound above.
        {
            DirectX::XMFLOAT3 camForward = camera.lookDirection();
            DirectX::XMVECTOR forwardVec = DirectX::XMLoadFloat3(&camForward);
            DirectX::XMVECTOR worldUpVec = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR rightVec = DirectX::XMVector3Normalize(
                DirectX::XMVector3Cross(worldUpVec, forwardVec));
            DirectX::XMVECTOR upVec = DirectX::XMVector3Cross(forwardVec, rightVec);
            DirectX::XMFLOAT3 camRight;
            DirectX::XMFLOAT3 camUp;
            DirectX::XMStoreFloat3(&camRight, rightVec);
            DirectX::XMStoreFloat3(&camUp, upVec);

            if(auto t=targetSelection.selected(inspectionTargets);t&&t->resolved)
                gfx.drawCircle({t->position.x,t->position.y+.08f,t->position.z},1.1f,{1.f,.85f,.2f,1.f});
            gfx.beginLabelPass();
            constexpr float kLabelWorldHeight = 0.4f; // world units - tuned by eye, not measured
            constexpr float kLabelGapAboveBox = 0.3f;
            forEachObject([&](const auto& obj) {
                using T = std::decay_t<decltype(obj)>;
                if constexpr (std::is_same_v<T, worldmodel::CellObject> ||
                              std::is_same_v<T, worldmodel::GroupObject>) {
                    return;
                } else {
                    if (obj.transformMessagesSeen == 0) {
                        return;
                    }
                    // Same "skip rather than guess" rule as the mesh-drawing
                    // pass above - see resolveWorldPosition()'s own comment.
                    std::optional<DirectX::XMFLOAT3> resolvedLabelPos;
                    if (!obj.isSelf) {
                        resolvedLabelPos = resolveWorldPosition(worldSnapshot, obj);
                        if (!resolvedLabelPos.has_value()) {
                            return;
                        }
                    }
                    auto name = labelTextFor(obj);
                    if (!name.has_value()) {
                        return;
                    }
                    if constexpr(std::is_same_v<T,worldmodel::CreatureObject>)
                        *name=clientcommon::targetDisplayName(*name);
                    std::string key = toUtf8Preview(*name);
                    auto it = labelCache.find(key);
                    if (it == labelCache.end()) {
                        int pixelWidth = 0;
                        int pixelHeight = 0;
                        auto texture = gfx.createTextTexture(toWString(*name), pixelWidth, pixelHeight);
                        float aspectRatio =
                            static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
                        it = labelCache.emplace(key, LabelTexture{texture, aspectRatio}).first;
                    }

                    DirectX::XMFLOAT3 halfExtents = visualizerBoxHalfExtentsFor(obj.typeTag);
                    // Self's label follows the same locally-predicted
                    // position as its box (Phase 13) - see the box-drawing
                    // pass above for why. objPos.y is ground/feet level and
                    // the box sits ON TOP of it, so the label's true
                    // anchor is objPos.y + 2*halfExtents.y, not
                    // objPos.y + halfExtents.y.
                    DirectX::XMFLOAT3 objPos = obj.isSelf ? predictedSelfPos : *resolvedLabelPos;
                    if(auto presented=remotePoses.find(obj.objectId);presented!=remotePoses.end())
                        objPos={presented->second.x,presented->second.y,presented->second.z};
                    DirectX::XMFLOAT3 labelCenter{
                        objPos.x, objPos.y + 2.0f * halfExtents.y + kLabelGapAboveBox, objPos.z};
                    float labelHeight = kLabelWorldHeight;
                    float labelWidth = labelHeight * it->second.aspectRatio;
                    gfx.drawLabel(labelCenter, labelWidth, labelHeight, camRight, camUp,
                                  it->second.texture);
                }
            });

            // Phase 17 Step 5 - real elevator interaction. hoveredObjectId
            // was found (if at all) by the ray-cursor pick test folded into
            // the draw pass above; only elevator terminals are wired to a
            // real outbound action today (see elevatorDirectionFor()'s own
            // comment on why the plain, direction-less "terminal_elevator"
            // variant is deliberately left unhandled). A real, live-
            // verified round trip: ObjectMenuSelect -> server moves self
            // via DataTransformWithParent, picked up automatically by
            // ObjectStore/resolveWorldPosition, same as any other real
            // containment update.
            if (hoveredObjectId != 0) {
                ElevatorDirection elevatorDir =
                    elevatorDirectionFor(meshResolver.templatePathFor(hoveredObjectCrc));
                if (elevatorDir != ElevatorDirection::None) {
                    const std::u16string promptText = (elevatorDir == ElevatorDirection::Up)
                                                            ? u"[Click] Take Elevator Up"
                                                            : u"[Click] Take Elevator Down";
                    std::string key = "prompt:" + toUtf8Preview(promptText);
                    auto it = labelCache.find(key);
                    if (it == labelCache.end()) {
                        int pixelWidth = 0;
                        int pixelHeight = 0;
                        auto texture = gfx.createTextTexture(toWString(promptText), pixelWidth,
                                                               pixelHeight);
                        float aspectRatio =
                            static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
                        it = labelCache.emplace(key, LabelTexture{texture, aspectRatio}).first;
                    }
                    DirectX::XMFLOAT3 promptCenter{hoveredObjectPos.x, hoveredObjectPos.y + 1.0f,
                                                    hoveredObjectPos.z};
                    float promptHeight = kLabelWorldHeight;
                    float promptWidth = promptHeight * it->second.aspectRatio;
                    gfx.drawLabel(promptCenter, promptWidth, promptHeight, camRight, camUp,
                                  it->second.texture);

                    bool leftMouseDown = renderer::Window::isMouseButtonDown(0);
                    if (leftMouseDown && !leftMouseWasDown) {
                        uint8_t radialId = (elevatorDir == ElevatorDirection::Up) ? 198 : 199;
                        if(networkThread)networkThread->enqueueSend(
                            swgproto::buildObjectMenuSelect(hoveredObjectId, radialId));
                        std::cout << "[INTERACT] Sent ObjectMenuSelect objectId=" << hoveredObjectId
                                   << " radialId=" << static_cast<int>(radialId) << " (elevator "
                                   << (elevatorDir == ElevatorDirection::Up ? "up" : "down")
                                   << ")\n";
                    }
                }
            }
            leftMouseWasDown = renderer::Window::isMouseButtonDown(0);
        }

        // Top-down minimap, user-requested 2026-07-18: the main perspective
        // view alone left the user unable to tell "is that object above me,
        // or just far away" - an inherent ambiguity of any perspective
        // projection on a flat screen, not something the ground grid/walls
        // could fully resolve. A straight-down orthographic view sidesteps
        // that entirely (no depth axis to misread), at the cost of losing
        // vertical information entirely - a deliberate, explicit tradeoff,
        // not a replacement for the main view. Fixed-size corner overlay,
        // covering a 100x100 world-unit (50m radius) area centered on self,
        // recentered every frame like the ground grid/walls above.
        {
            constexpr int kMinimapSize = 480;
            constexpr int kMinimapMargin = 20;
            int minimapX = window.width() - kMinimapSize - kMinimapMargin;
            int minimapY = kMinimapMargin;
            gfx.beginMinimapPass(minimapX, minimapY, kMinimapSize, kMinimapSize);

            constexpr float kMinimapRadius = 50.0f; // world units - matches the ortho extent below
            DirectX::XMVECTOR selfVec = DirectX::XMLoadFloat3(&predictedSelfPos);
            DirectX::XMVECTOR eyeVec = DirectX::XMVectorAdd(
                selfVec, DirectX::XMVectorSet(0.0f, 100.0f, 0.0f, 0.0f));
            // Straight down, "up" = world +Z (matches the main view's own
            // yaw=0 -> +Z convention, so the minimap's orientation lines up
            // with the main scene's rather than introducing a second,
            // independent "north").
            DirectX::XMMATRIX minimapView = DirectX::XMMatrixLookAtLH(
                eyeVec, selfVec, DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
            DirectX::XMMATRIX minimapProjection = DirectX::XMMatrixOrthographicLH(
                kMinimapRadius * 2.0f, kMinimapRadius * 2.0f, 0.1f, 200.0f);
            gfx.setViewProjection(minimapView, minimapProjection);

            DirectX::XMFLOAT4 ringColor{0.4f, 0.4f, 0.45f, 1.0f};
            gfx.drawCircle(predictedSelfPos, 10.0f, ringColor);
            gfx.drawCircle(predictedSelfPos, 30.0f, ringColor);

            constexpr float kMinimapDotRadius = 1.5f;
            forEachObject([&](const auto& obj) {
                using T = std::decay_t<decltype(obj)>;
                if constexpr (std::is_same_v<T, worldmodel::CellObject> ||
                              std::is_same_v<T, worldmodel::GroupObject>) {
                    return;
                } else {
                    if (obj.transformMessagesSeen == 0 || obj.isSelf) {
                        return; // self gets its own distinct marker below
                    }
                    auto resolved=resolveWorldPosition(worldSnapshot,obj);
                    if(!resolved)return;
                    DirectX::XMFLOAT3 dotCenter{resolved->x, predictedSelfPos.y, resolved->z};
                    if(auto presented=remotePoses.find(obj.objectId);presented!=remotePoses.end()) {
                        dotCenter.x=presented->second.x;dotCenter.z=presented->second.z;
                    }
                    DirectX::XMFLOAT4 color = visualizerColorFor(obj.typeTag, false);
                    gfx.drawCircle(dotCenter, kMinimapDotRadius, color);
                }
            });

            // Self: a distinct marker (matches the beacon's magenta) plus a
            // short facing tick so the minimap shows which way self is
            // looking, not just where self is.
            DirectX::XMFLOAT4 selfColor{1.0f, 0.2f, 1.0f, 1.0f};
            gfx.drawCircle(predictedSelfPos, kMinimapDotRadius * 1.5f, selfColor);

            DirectX::XMFLOAT3 facingDir{sinf(predictedSelfYawRadians), 0.0f,
                                         cosf(predictedSelfYawRadians)};
            DirectX::XMFLOAT3 facingCenter{
                predictedSelfPos.x + facingDir.x * 5.0f, predictedSelfPos.y,
                predictedSelfPos.z + facingDir.z * 5.0f};
            DirectX::XMFLOAT3 facingHalfExtents{0.4f, 0.1f, 3.0f};
            gfx.drawWireBox(facingCenter, facingHalfExtents, predictedSelfYawRadians, selfColor);
        }

        if(nativeClient&&commandCatalog&&commandSubmission&&chatLog){
            auto notice=[&](std::u16string text){clientcommon::ChatLine line;line.text=std::move(text);line.delivery=clientcommon::ChatLine::Delivery::CurrentTab;chatLog->append(std::move(line));};
            auto feedback=[&](clientcommon::ChatLine line){line.delivery=clientcommon::ChatLine::Delivery::CurrentTab;chatLog->append(std::move(line));};
            auto selected=targetSelection.selected(inspectionTargets);chatContext.target=selected&&selected->resolved?selected->id:0;
            auto submitParsed=[&](const dummyclient::ParsedCommand& parsed)->bool{
                if(!chatContext.actor||!networkThread||networkThread->failed())return false;
                if(auto ticket=commandSubmission->reserve(chatContext.actor,parsed.target,parsed.name,chatElapsed())){
                    auto payload=swgproto::buildCommandQueueEnqueue(ticket->actor,ticket->action,parsed.name,ticket->target,parsed.arguments);
                    const bool accepted=networkThread->tryEnqueueCommand(std::move(payload));commandSubmission->admitted(*ticket,accepted);return accepted;
                }
                return false;
            };
            if(auto draft=window.takeTextSubmission()){
                bool accepted=false;dummyclient::ParsedTell tell;
                std::optional<std::u16string> selectedPlayerName;
                if(tellSubmission){
                    auto nameStart=draft->find_first_not_of(u" ");auto nameEnd=draft->find_first_of(u" ",nameStart==draft->npos?0:nameStart);
                    auto commandName=nameStart==draft->npos?std::u16string{}:draft->substr(nameStart,nameEnd==draft->npos?draft->size()-nameStart:nameEnd-nameStart);
                    std::string commandAscii;for(auto c:commandName)if(c<128)commandAscii+=char(c);
                    if(clientcommon::lowerTell(commandAscii)=="/ttell"&&selected&&selected->resolved){
                        std::optional<std::string> path;
                        forEachObject([&](const auto& obj){if(obj.objectId==selected->id)if(auto candidate=skeletalMeshResolver.templatePathFor(obj.objectCrc))path=*candidate;});
                        if(path&&assets::gameObjectType(*path,[&](const auto& resource){return meshResolver.extract(resource);})==1025)selectedPlayerName=selected->name;
                    }
                    tell=dummyclient::parseTell(*draft,*commandCatalog,tellSubmission->context(),selectedPlayerName);
                }
                auto parsed=commandCatalog->parse(*draft,chatContext);
                const auto first=draft->find_first_not_of(u" ");
                if(chatLog->activeTab()==2&&first!=draft->npos&&(*draft)[first]!=u'/')notice(u"Planet chat is not connected yet. Select Spatial or use /say or /tell. Your draft has been kept.");
                else if(tell.recognized){
                    const auto self=tellSubmission->context().self;
                    if(!tell.nativeError.empty())notice(std::move(tell.nativeError));
                    else if(!tell.errorKey.empty())feedback(clientcommon::tellLine(tell.errorKey,{},u"",self));
                    else if(!chatContext.actor||!networkThread||networkThread->failed())notice(u"The world is not ready to send tells. Your draft has been kept.");
                    else if(auto tickets=tellSubmission->reserveBatch(tell.targets,tell.text,tell.reply,chatElapsed())){
                        std::vector<std::vector<uint8_t>> payloads;for(const auto& ticket:*tickets)payloads.push_back(swgproto::buildChatInstantMessage(ticket.recipient,ticket.text,ticket.sequence));
                        accepted=networkThread->tryEnqueueTells(std::move(payloads));for(const auto& ticket:*tickets)tellSubmission->admitted(ticket,accepted);
                        if(accepted){for(const auto& duplicate:tell.duplicates)feedback(clientcommon::tellLine("im_message_dupe_discarded_prose",duplicate,u"",self));
                            if(tell.multiple)feedback(clientcommon::tellLine("im_messages_sent",{},u"",self));}
                    }else notice(u"Please wait before sending another tell. Your draft has been kept.");
                }
                else if(parsed.empty)accepted=true;
                else if(!parsed.error.empty())notice(std::move(parsed.error));
                else if(!chatContext.actor||!networkThread||networkThread->failed())notice(u"The world is not ready to send commands. Your draft has been kept.");
                else if(!(accepted=submitParsed(parsed)))notice(u"Please wait before sending another command. Your draft has been kept.");
                window.finishTextSubmission(accepted);
            }
            std::optional<size_t> action=window.takeActionSelection();
            for(size_t i=0;i<clientcommon::ActionToolbar::count;++i)if(window.takeKeyPress(int('1'+i)))action=i;
            if(action){
                if(auto name=actionToolbar.request(*action,chatContext.target!=0,window.textFocus().active(),chatElapsed())){
                    std::u16string typed=u"/";typed.append(name->begin(),name->end());
                    auto parsed=commandCatalog->parse(typed,chatContext);
                    if(!parsed.error.empty())notice(std::move(parsed.error));
                    else if(!submitParsed(parsed))notice(u"The action could not be sent. Please try again.");
                }
            }
            if(tellSubmission)for(const auto& event:tellSubmission->drain(chatElapsed())){
                if(event.kind==clientcommon::TellEvent::Kind::Result){auto line=clientcommon::tellLine(clientcommon::tellResultKey(event.result,event.ticket.reply),event.ticket.recipient,event.result?u"":event.ticket.text,tellSubmission->context().self);line.delivery=clientcommon::ChatLine::Delivery::AllTabs;chatLog->append(std::move(line));}
                else if(event.kind==clientcommon::TellEvent::Kind::TransportRejected)notice(u"The tell was not sent. Your draft has been kept.");
                else if(event.kind==clientcommon::TellEvent::Kind::NoReply)notice(u"No delivery confirmation was received for your tell.");
                if(chatJournal&&chatJournalEvents<512){++chatJournalEvents;
                    const auto* kind=event.kind==clientcommon::TellEvent::Kind::Admitted?"tell_admitted":event.kind==clientcommon::TellEvent::Kind::Result?"tell_result":event.kind==clientcommon::TellEvent::Kind::NoReply?"tell_no_reply":"tell_not_sent";
                    chatJournal<<"{\"event\":\""<<kind<<"\",\"elapsed_ms\":"<<chatElapsed()<<",\"sequence\":"<<event.ticket.sequence<<",\"result\":"<<event.result<<",\"reply\":"<<(event.ticket.reply?"true":"false")<<",\"body_units\":"<<event.ticket.text.size()<<",\"pending\":"<<tellSubmission->pending()<<"}\n";chatJournal.flush();
                }
            }
            for(const auto& event:commandSubmission->drain(chatElapsed())){
                actionToolbar.onEvent(event,chatElapsed());
                if(auto text=commandCatalog->errorText(event))notice(std::move(*text));
                if(chatJournal&&chatJournalEvents<512){++chatJournalEvents;
                    const char* name=event.kind==clientcommon::CommandEvent::Kind::Admitted?"command_admitted":event.kind==clientcommon::CommandEvent::Kind::TransportRejected?"command_not_sent":event.kind==clientcommon::CommandEvent::Kind::QueueCleared?"command_queue_clear":"command_no_reply";
                    chatJournal<<"{\"event\":\""<<name<<"\",\"elapsed_ms\":"<<chatElapsed()<<",\"action\":"<<event.ticket.action<<",\"crc\":"<<event.ticket.crc<<",\"actor\":"<<event.ticket.actor<<",\"target\":"<<event.ticket.target<<",\"error\":"<<event.error<<",\"detail\":"<<event.detail<<"}\n";chatJournal.flush();
                }
            }
        }
        if(targetHud)targetHud->draw(targetSelection.selected(inspectionTargets),inspectionSelf,window.pixelDensity(),inspectionPlayer?&*inspectionPlayer:nullptr);
        if(nativeClient&&targetHud){
            if(lastPresentedRawRevision!=chatSnapshot.revision||lastSelectedRawRevision!=chatSelected.revision||(unresolvedLocalized&&chatElapsed()-lastLocalizedResolveMs>=1000)){
                auto nextPresentation=chatSnapshot;const auto globalCount=nextPresentation.lines.size();
                for(const auto& line:chatSelected.lines)if(std::none_of(nextPresentation.lines.begin(),nextPresentation.lines.end(),[&](const auto& other){return other.sequence==line.sequence;}))nextPresentation.lines.push_back(line);
                if(lastPresentedRawRevision!=chatSnapshot.revision||lastSelectedRawRevision!=chatSelected.revision)++resolveEpoch;
                nextPresentation.revision=resolveEpoch;lastSelectedRawRevision=chatSelected.revision;lastPresentedRawRevision=chatSnapshot.revision;unresolvedLocalized=false;lastLocalizedResolveMs=chatElapsed();
                if(localizedChat){localizedChat->beginPass();auto sequences=chatLog->retainedSequences();localizedChat->retainSequences(sequences);}size_t localizedResolved=0,localizedDeferred=0;
                std::unordered_map<uint64_t,dummyclient::ChatObjectState> chatObjects;
                forEachObject([&](const auto& object){if(chatObjects.size()<8192)chatObjects.emplace(object.objectId,dummyclient::chatObjectState(object));});
                auto participantContext=[&](uint64_t id)->std::optional<dummyclient::LocalizedChat::ObjectInfo>{
                    const auto found=chatObjects.find(id);if(found==chatObjects.end())return {};const auto& state=found->second;dummyclient::LocalizedChat::ObjectInfo info;info.creature=state.creature;
                    const auto* templatePath=state.creature?skeletalMeshResolver.templatePathFor(state.objectCrc):meshResolver.templatePathFor(state.objectCrc);
                    if(state.nameBaseline){
                        if(!state.customName.empty())info.name=state.customName;
                        else if(swgproto::isValidStringIdReference(state.nameId))info.name=localizedChat?localizedChat->resolveName(state.nameId):std::nullopt;
                        else if(state.nameId.isInvalid()&&templatePath){auto path=state.creature?*templatePath:toSharedTemplatePath(*templatePath);info.name=std::u16string(path.begin(),path.end());}
                    }
                    if(state.creature&&state.appearanceKnown&&templatePath){info.templatePath=*templatePath;info.alternateAppearance=state.alternateAppearance;}
                    return info;
                };
                auto speakerName=[&](uint64_t id)->std::optional<std::u16string>{auto info=participantContext(id);if(!info||!info->name)return {};return clientcommon::targetDisplayName(*info->name);};
                for(const auto index:localizedSchedule.order(nextPresentation)){auto& line=nextPresentation.lines[index];
                    if(line.localized){
                        auto resolved=localizedChat?localizedChat->formatRetainedWithContext(line.sequence,*line.localized,participantContext):std::nullopt;
                        if(resolved){line.text=std::move(*resolved);++localizedResolved;}else {line.text.clear();unresolvedLocalized=true;++localizedDeferred;}
                    }
                    if(commandCatalog&&!line.text.empty())line.text=clientcommon::formatNeutralSpatialChat(line,speakerName(line.sender).value_or(u""),commandCatalog->sayVerb(),commandCatalog->sayType());
                }
                auto selectedPresentation=chatSelected;for(auto& line:selectedPresentation.lines){auto found=std::find_if(nextPresentation.lines.begin(),nextPresentation.lines.end(),[&](const auto& other){return other.sequence==line.sequence;});if(found!=nextPresentation.lines.end())line=*found;}
                nextPresentation.lines.resize(globalCount);
                clientcommon::updateChatPresentation(chatPresentation,std::move(nextPresentation));
                clientcommon::updateChatPresentation(chatSelectedPresentation,std::move(selectedPresentation));
                if(chatJournal&&chatJournalEvents<512&&(lastLocalizedRevision!=chatSnapshot.revision||lastLocalizedResolved!=localizedResolved||lastLocalizedDeferred!=localizedDeferred)){
                    ++chatJournalEvents;lastLocalizedRevision=chatSnapshot.revision;lastLocalizedResolved=localizedResolved;lastLocalizedDeferred=localizedDeferred;
                    chatJournal<<"{\"event\":\"localization\",\"elapsed_ms\":"<<chatElapsed()<<",\"revision\":"<<chatSnapshot.revision<<",\"resolved\":"<<localizedResolved<<",\"deferred\":"<<localizedDeferred<<",\"cached_tables\":"<<(localizedChat?localizedChat->cachedTables():0)<<",\"table_loads\":"<<(localizedChat?localizedChat->tableLoads():0)<<",\"metadata_loads\":"<<(localizedChat?localizedChat->metadataLoads():0)<<",\"cached_templates\":"<<(localizedChat?localizedChat->cachedTemplates():0)<<"}\n";chatJournal.flush();
                }
            }
            std::optional<size_t> hover;int hoverX=0,hoverY=0;window.cursorPosition(hoverX,hoverY);const auto& previousRects=targetHud->chatTabRects();
            for(size_t i=0;i<previousRects.size();++i){const auto& r=previousRects[i];if(hoverX>=r[0]&&hoverX<r[2]&&hoverY>=r[1]&&hoverY<r[3]){hover=i;break;}}
            window.setTextEntryRect(targetHud->drawChat(chatSelectedPresentation,window.textFocus(),window.pixelDensity(),window.takeChatWheelDelta(),chatLog?chatLog->activeTab():0,hover));window.setChatAreaRect(targetHud->chatAreaRect());window.setChatTabRects(targetHud->chatTabRects());
            targetHud->drawNotices(chatSnapshot,chatPresentation,window.pixelDensity());
            window.setActionRects(targetHud->drawActions(actionToolbar,chatContext.target!=0,chatElapsed(),window.pixelDensity()));
            if(chatJournal&&chatJournalEvents<512&&targetHud->noticeState()){
                const auto& state=*targetHud->noticeState();std::ostringstream notice;
                notice<<"\"rows\":"<<state.rows().size()<<",\"admissions\":"<<state.admissions();
                if(!state.rows().empty())notice<<",\"oldest_sequence\":"<<state.rows().front().sequence<<",\"enabled\":"<<(state.rows().front().enabled?"true":"false");
                if(notice.str()!=lastNoticeRecord){lastNoticeRecord=notice.str();++chatJournalEvents;
                    chatJournal<<"{\"event\":\"notice\",\"elapsed_ms\":"<<chatElapsed()<<','<<lastNoticeRecord<<"}\n";chatJournal.flush();}
            }
        }

        if(offline&&!offlineTitleReady&&terrainChunkHandles.size()>=25&&selfAnimData&&selfDynamicMeshes.size()>0) {
            window.setTitle(L"SWG Offline World — WASD move · R walk/run · E action · C stop action · M ambient · drag orbit · wheel zoom · Tab targets · Q point · Backspace clear · I inspect · 0 reset · Esc quit");offlineTitleReady=true;
        }
        if(nativeClient&&!offlineTitleReady&&terrainChunkHandles.size()>=25&&selfAnimData&&!selfDynamicMeshes.empty()) {
            window.setTitle(L"SWG Native Client — WASD move · R walk/run · E action · C stop action · M ambient · drag orbit · wheel zoom · Tab targets · Q point · Backspace clear · Esc quit");
            // The launcher consumes a pipe, so newline alone may leave this
            // readiness event buffered until its 95-second startup deadline.
            offlineTitleReady=true;std::cout<<"NATIVE_WORLD_READY terrain_chunks="<<terrainChunkHandles.size()<<"\n"<<std::flush;
        }
        if(smoke&&smokePhase<0&&(!materialSmoke||materialFixtureTexturedDraws==10)&&terrainChunkHandles.size()>=25&&selfAnimData&&selfDynamicMeshes.size()>0&&buildingCache.get(soe::MessageHash::compute("object/building/player/shared_player_guildhall_generic_style_01.iff"),assetWorker)) {
            smokePhase=0;smokeFrames=0;std::cout<<"[OFFLINE TEST] complete world ready\n";
            if(cellSmoke) {
                // The fixture starts at an already supported porch position,
                // after asset readiness. Startup terrain fallback is not the
                // doorway state-machine scenario under test.
                predictedSelfPos=offlineSpawn;persistentCellIndex=0;persistentCellBuildingId=offlineBuildingId;
                wasInsideBuildingFootprintLastFrame=true;predictedSelfParentId=0;cellTraversal.select(offlineBuildingId,0);
            }
            if(actionSmoke) {offlinePlayback->reset();offlinePlayback->playAction("look_casual_ag");}
        }
        maxTerrainChunks=std::max(maxTerrainChunks,terrainChunkHandles.size());
        const bool materialCapture=materialSmoke&&smokePhase==0&&smokeFrames==15;
        bool smokeCapture=smoke&&!actionSmoke&&!cameraSmoke&&!floorSmoke&&!cellSmoke&&!materialSmoke&&smokePhase>=0&&smokeFrames==(smokePhase==1?68:smokePhase==3?240:15);
        const bool transitionCapture=smoke&&!actionSmoke&&!cameraSmoke&&!floorSmoke&&!cellSmoke&&smokePhase==1&&(smokeFrames==0||smokeFrames==3||smokeFrames==7);
        const bool actionCapture=actionSmoke&&smokePhase>=0&&(smokeFrames==8||smokeFrames==21||smokeFrames==25||smokeFrames==54||smokeFrames==62||smokeFrames==250||smokeFrames==260||smokeFrames==280);
        if(actionSmoke&&smokePhase>=0) {
            const auto& record=offlinePlayback->records().at(0);
            actionTraces.push_back({smokeFrames,record.mode,offlinePlayback->records().back().mode,record.weight,
                offlinePlayback->actionFrame(),offlinePlayback->idleFrame(),offlinePlayback->replacementWeight(),
                offlinePlayback->actionActive(),offlinePlayback->actionRouted(),offlinePlayback->actionName()});
        }
        const bool cameraCapture=cameraSmoke&&smokePhase>=0&&(smokeFrames==60||smokeFrames==150||smokeFrames==240);
        const bool floorCapture=floorSmoke&&smokePhase>=0&&(smokeFrames==60||smokeFrames==450||smokeFrames==880);
        const bool cellCapture=cellSmoke&&smokePhase>=0&&(smokeFrames==60||smokeFrames==220||smokeFrames==450);
        if(screenshotCapture.burstCaptureActive||cellCapture||smokeCapture||gaitProfileCapture||transitionCapture||actionCapture||materialCapture||cameraCapture||floorCapture) gfx.requestFrameCapture();
        gfx.endFrame();

        // Burst screenshot capture - see ScreenshotCapture::captureIfActive's
        // own comment. Must run right after endFrame() and before the next
        // beginFrame() - see captureFrameRGBA8's own comment on why.
        screenshotCapture.captureIfActive(gfx);
        if(cellCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height)||!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/("cell-"+std::to_string(smokeFrames)+".png")).string(),width,height,pixels))throw std::runtime_error("Cell smoke: capture failed");
        }
        if(cellSmoke&&smokePhase>=0&&smokeFrames==470) {
            if((frameSmoke&&frameModelChecks<470)||cellEnteredFrames!=90||cellExitedFrames!=90||cellBlockedFrames!=30||cellEnterCommits!=1||cellExitCommits!=1)throw std::runtime_error("Cell smoke: incomplete entry/exit/block coverage");
            std::ofstream report(std::filesystem::path(offline->smokeOutput)/"cell-report.json");report.exceptions(std::ios::failbit|std::ios::badbit);
            report<<std::setprecision(9)<<"{\"passed\":true,\"architecture\":\"arm64\",\"server_contacted\":false,\"source\":\"actual_integrated_render_loop_candidate_replay\",\"mapping\":\"synthetic_server_cells\",\"entered_hold_frames\":"<<cellEnteredFrames
                <<",\"exited_hold_frames\":"<<cellExitedFrames<<",\"blocked_proposals\":"<<cellBlockedFrames<<",\"entry_commits\":"<<cellEnterCommits<<",\"exit_commits\":"<<cellExitCommits<<",\"max_wire_roundtrip_error_m\":"<<cellMaxWireError<<",\"full_rotation_fixture\":"<<(frameSmoke?"true":"false")<<",\"model_frame_checks\":"<<frameModelChecks<<",\"max_model_error_m\":"<<frameMaxModelError<<",\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"capabilities\":"<<gfx.capabilityReport()<<"}\n";
            report.close();smokeComplete=true;break;
        }
        if(floorCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height)||!dummyclient::writePngRGBA8(
                (std::filesystem::path(offline->smokeOutput)/("floor-"+std::to_string(smokeFrames)+".png")).string(),width,height,pixels))
                throw std::runtime_error("Floor smoke: GPU capture failed");
        }
        if(floorSmoke&&smokePhase>=0&&smokeFrames==920) {
            if(floorTerrainFrames<90||floorMeshFrames<90||floorBuriedFrames<90||floorMinLocalZ>18.1f||floorRampCheckFrames<300)
                throw std::runtime_error("Floor smoke: missing fixture/ramp coverage");
            std::ofstream report(std::filesystem::path(offline->smokeOutput)/"floor-report.json");report.exceptions(std::ios::failbit|std::ios::badbit);
            report<<std::setprecision(9)<<"{\"passed\":true,\"architecture\":\"arm64\",\"server_contacted\":false,\"source\":\"actual_integrated_render_loop_candidate_replay\",\"terrain_frames\":"<<floorTerrainFrames
                <<",\"floor_mesh_frames\":"<<floorMeshFrames<<",\"buried_candidate_frames\":"<<floorBuriedFrames<<",\"minimum_local_z\":"<<floorMinLocalZ
                <<",\"max_stationary_height_error\":"<<floorMaxStationaryError<<",\"max_height_step\":"<<floorMaxStep
                <<",\"ramp_height_check_frames\":"<<floorRampCheckFrames<<",\"max_ramp_height_error\":"<<floorMaxRampError
                <<",\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"capabilities\":"<<gfx.capabilityReport()<<"}\n";
            report.close();smokeComplete=true;break;
        }
        if(cameraCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height)||!dummyclient::writePngRGBA8(
                (std::filesystem::path(offline->smokeOutput)/("camera-"+std::to_string(cameraProbePhase)+".png")).string(),width,height,pixels))
                throw std::runtime_error("Camera smoke: GPU capture failed");
        }
        if(cameraSmoke&&smokePhase>=0&&smokeFrames==270) {
            if(cameraBuildingFrames<30||cameraTerrainFrames<30||cameraOverlapFrames)
                throw std::runtime_error("Camera smoke: missing obstruction coverage or invalid target");
            std::ofstream report(std::filesystem::path(offline->smokeOutput)/"camera-report.json");report.exceptions(std::ios::failbit|std::ios::badbit);
            report<<"{\"passed\":true,\"architecture\":\"arm64\",\"server_contacted\":false,\"source\":\"actual_integrated_render_loop\",\"building_blocked_frames\":"<<cameraBuildingFrames
                <<",\"terrain_blocked_frames\":"<<cameraTerrainFrames<<",\"initial_overlap_frames\":"<<cameraOverlapFrames
                <<",\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"capabilities\":"<<gfx.capabilityReport()<<"}\n";
            report.close();smokeComplete=true;break;
        }
        if(materialCapture) {
            if(materialFixtureTexturedDraws!=10)throw std::runtime_error("Fixture materials did not reach the live draw path");
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            gfx.captureFrameRGBA8(pixels,width,height);
            if(!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/"creature-materials.png").string(),width,height,pixels))
                throw std::runtime_error("Material capture failed");
            std::ofstream report(std::filesystem::path(offline->smokeOutput)/"material-report.json");
            report.exceptions(std::ios::failbit|std::ios::badbit);
            report<<"{\"source\":\"actual_integrated_render_loop\",\"architecture\":\"arm64\",\"server_contacted\":false,\"remote_creature_instances\":2,\"textured_submesh_draws_in_capture\":"
                <<materialFixtureTexturedDraws<<",\"fallback_draws_in_capture\":0,\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"capabilities\":"<<gfx.capabilityReport()<<"}";
            report.close();smokeComplete=true;break;
        }
        if(actionCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            gfx.captureFrameRGBA8(pixels,width,height);
            if(!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/("action-"+std::to_string(smokeFrames)+".png")).string(),width,height,pixels))
                throw std::runtime_error("Action PNG write failed");
        }
        if(actionSmoke&&smokePhase>=0&&smokeFrames==290) {
            const auto findFrame=[&](int frame)->const ActionTrace& {
                const auto found=std::find_if(actionTraces.begin(),actionTraces.end(),[&](const auto& trace){return trace.frame==frame;});
                if(found==actionTraces.end())throw std::runtime_error("Missing integrated action trace");return *found;
            };
            if(findFrame(8).mode!=0||findFrame(21).replacementWeight<=0||findFrame(21).replacementWeight>=1||
               findFrame(54).active||findFrame(54).mode!=1||findFrame(54).weight<=0||findFrame(54).weight>=1||
               findFrame(65).routed||findFrame(65).mode!=2||!findFrame(250).active||
               findFrame(280).active||findFrame(280).routed||findFrame(280).mode!=2)
                throw std::runtime_error("Integrated action lifecycle assertion failed");
            std::ofstream report(std::filesystem::path(offline->smokeOutput)/"action-report.json");
            report.exceptions(std::ios::failbit|std::ios::badbit);report<<std::setprecision(9);
            report<<"{\"architecture\":\"arm64\",\"server_contacted\":false,\"source\":\"actual_integrated_render_loop\",\"action_count\":"<<offlineActionNames.size()
                <<",\"unsupported_action_count\":"<<offlineUnsupportedActions.size()<<",\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"replacement_seconds\":0.15,\"priority_seconds\":0.25,\"passed\":true,\"traces\":[";
            for(size_t i=0;i<actionTraces.size();++i) {if(i)report<<",";const auto& t=actionTraces[i];
                report<<"{\"frame\":"<<t.frame<<",\"mode\":"<<t.mode<<",\"weight\":"<<t.weight<<",\"locomotion_mode\":"<<t.locomotionMode
                    <<",\"action_frame\":"<<t.actionFrame<<",\"idle_frame\":"<<t.idleFrame<<",\"replacement_weight\":"<<t.replacementWeight
                    <<",\"active\":"<<(t.active?"true":"false")<<",\"routed\":"<<(t.routed?"true":"false")<<",\"name\":\""<<t.name<<"\"}";
            }
            report<<"]}";report.close();smokeComplete=true;break;
        }
        if(transitionCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height))throw std::runtime_error("Transition GPU capture failed");
            const auto name="world-start-"+std::to_string(smokeFrames+1)+".png";
            if(!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/name).string(),width,height,pixels))
                throw std::runtime_error("Transition PNG write failed");
        }
        if(gaitProfileCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height))throw std::runtime_error("Gait profile GPU capture failed");
            std::string name=smokeFrames==12?"world-knee-left.png":"world-knee-right.png";
            if(!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/name).string(),width,height,pixels))
                throw std::runtime_error("Gait profile PNG write failed");
        }
        if(smokeCapture) {
            std::vector<uint8_t> pixels;uint32_t width=0,height=0;
            if(!gfx.captureFrameRGBA8(pixels,width,height))throw std::runtime_error("Offline world GPU capture failed");
            std::string name=smokePhase==0?"world-idle":smokePhase==1?"world-walk":smokePhase==2?"world-interior":smokePhase==3?"world-wall-test":"world-overview";
            if(!dummyclient::writePngRGBA8((std::filesystem::path(offline->smokeOutput)/(name+".png")).string(),width,height,pixels))throw std::runtime_error("World PNG write failed");
            std::cout<<"[OFFLINE TEST] captured "<<name<<" position="<<predictedSelfPos.x<<","<<predictedSelfPos.y<<","<<predictedSelfPos.z<<"\n";
            if(smokePhase==0)smokeWalkStart=predictedSelfPos;
            if(smokePhase==1) {
                float dx=predictedSelfPos.x-smokeWalkStart.x,dz=predictedSelfPos.z-smokeWalkStart.z;smokeWalkDistance=std::sqrt(dx*dx+dz*dz);
                if(std::abs(dx-smokeExpectedWalkDelta.x)>.025f||std::abs(dz-smokeExpectedWalkDelta.z)>.025f)
                    throw std::runtime_error("Scripted movement does not match authored LOCT path");
                predictedSelfPos={offlineBuildingOrigin.x+offlineEvidence.interiorPoint.x,offlineBuildingOrigin.y+offlineEvidence.interiorPoint.y,offlineBuildingOrigin.z+offlineEvidence.interiorPoint.z};
                persistentCellIndex=offlineEvidence.interiorCell;persistentCellBuildingId=offlineBuildingId;
                persistentFloorTriangleIndex.reset();persistentFloorTriangleCellIndex.reset();
                camera.distance=4;camera.pitch=-.1f;
            }
            if(smokePhase==2) {camera.yaw=-DirectX::XM_PIDIV2;}
            if(smokePhase==3) {
                if(smokeBlockedFrames==0)throw std::runtime_error("Integrated movement did not stop at a real wall");
                inspectionMode=true;
                flyCamera.position={offlineBuildingOrigin.x+60,offlineBuildingOrigin.y+80,offlineBuildingOrigin.z+155};
                flyCamera.yaw=DirectX::XM_PI;flyCamera.pitch=-.4f;
            }
            if(smokePhase==4) {
                float leftMax=0,rightMax=0;
                for(const auto& sample:smokeKneeSamples){leftMax=std::max(leftMax,sample[1]);rightMax=std::max(rightMax,sample[2]);}
                // Independently recovered source-asset peaks are ~46 degrees.
                // Reject a frozen/straight-knee render-loop pose, without
                // changing the authored pose to satisfy a visual impression.
                if(smokeKneeSamples.size()<34||leftMax<44||rightMax<44)
                    throw std::runtime_error("Walk playback did not reach authored knee flexion");
                std::ofstream gait(std::filesystem::path(offline->smokeOutput)/"gait-report.json");
                gait<<std::setprecision(9);
                gait.exceptions(std::ios::failbit|std::ios::badbit);
                gait<<"{\"source\":\"actual_integrated_render_loop\",\"straight_knee_degrees\":0,\"left_max_bend_degrees\":"<<leftMax
                    <<",\"right_max_bend_degrees\":"<<rightMax<<",\"samples\":[";
                for(size_t i=0;i<smokeKneeSamples.size();++i){if(i)gait<<",";const auto& s=smokeKneeSamples[i];gait<<"["<<s[0]<<","<<s[1]<<","<<s[2]<<"]";}
                gait<<"],\"hip_samples\":[";
                for(size_t i=0;i<smokeHipSamples.size();++i){if(i)gait<<",";const auto& s=smokeHipSamples[i];gait<<"[";
                    for(size_t j=0;j<s.size();++j){if(j)gait<<",";gait<<s[j];}gait<<"]";}
                gait<<"],\"walk_start\":["<<smokeWalkStart.x<<","<<smokeWalkStart.y<<","<<smokeWalkStart.z
                    <<"],\"walk_start_frame\":"<<smokeWalkStartFrame
                    <<",\"transition_samples\":[";
                for(size_t i=0;i<smokeTransitionSamples.size();++i) {
                    if(i)gait<<",";const auto& s=smokeTransitionSamples[i];
                    gait<<"{\"elapsed_seconds\":"<<s.elapsed<<",\"idle_frame\":"<<s.idleFrame
                        <<",\"moving_frame\":"<<s.movingFrame<<",\"moving_weight\":"<<s.weight<<",\"local_matrices\":[";
                    for(size_t j=0;j<s.localMatrices.size();++j) {
                        if(j)gait<<",";gait<<"[";
                        for(size_t k=0;k<16;++k){if(k)gait<<",";gait<<s.localMatrices[j].m[k/4][k%4];}gait<<"]";
                    }
                    gait<<"]}";
                }
                gait<<"],\"speed_graph_children\":[";
                for(size_t i=0;i<offlineSpeedClipPaths.size();++i){if(i)gait<<",";gait<<"\""<<offlineSpeedClipPaths[i]<<"\"";}
                gait<<"],\"idle_to_moving_seconds\":0.25,\"moving_to_idle_seconds\":0.3"
                    <<",\"walk_yaw_radians\":"<<smokeWalkYawRadians
                    <<",\"root_motion\":\"authored_LOCT_XZ_on_ground\",\"profile_captures\":[\"world-knee-left.png\",\"world-knee-right.png\"]}";gait.close();
                std::ofstream report(std::filesystem::path(offline->smokeOutput)/"world-report.json");
                report.exceptions(std::ios::failbit|std::ios::badbit);
                report<<"{\"architecture\":\"arm64\",\"server_contacted\":false,\"capabilities\":"<<gfx.capabilityReport()
                    <<",\"terrain_chunks\":"<<terrainChunkHandles.size()<<",\"max_terrain_chunks_loaded\":"<<maxTerrainChunks<<",\"blocked_movement_frames\":"<<smokeBlockedFrames<<",\"building_instances\":2,\"building_cells\":"<<offlineCells.size()
                    <<",\"floor_height_checks\":"<<offlineEvidence.floorChecks<<",\"wall_crossing_checks\":"<<offlineEvidence.wallChecks
                    <<",\"animated_vertex_samples\":"<<offlineEvidence.animatedVertices<<",\"max_animation_delta\":"<<offlineEvidence.animationDelta
                    <<",\"walk_speed\":"<<offlineWalkSpeed<<",\"walk_fps\":"<<offlineWalkFps<<",\"walk_duration_frames\":"<<offlineWalkDurationFrames<<",\"walk_movement_frames\":"<<smokeWalkMovementFrames<<",\"walk_distance\":"<<smokeWalkDistance<<",\"interior_cell\":"<<offlineEvidence.interiorCell<<",\"captures\":[\"world-idle.png\",\"world-walk.png\",\"world-interior.png\",\"world-wall-test.png\",\"world-overview.png\"]}";
                report.close();
                smokeComplete=true;
                break;
            }
            ++smokePhase;smokeFrames=-1;
        }
        if(smoke&&smokePhase>=0)++smokeFrames;

        auto frameElapsed = Clock::now() - now;
        if (frameElapsed < targetFrameTime) {
            std::this_thread::sleep_for(targetFrameTime - frameElapsed);
        }
    }

    for(auto& handle:selfDynamicMeshes)gfx.destroyDynamicMesh(handle);
    if(smoke&&!smokeComplete)throw std::runtime_error("Offline smoke interrupted before completion");
    std::cout << "[VISUALIZER] window closed.\n";
}

std::vector<dummyclient::CreatureMaterials> dummyclient::resolveCreatureMaterials(
    const std::string& clientPath, const std::vector<std::string>& paths) {
    RealMeshResolver materials(clientPath);
    RealSkeletalMeshResolver skeletal(clientPath);
    std::vector<CreatureMaterials> result;
    for (const auto& path : paths) {
        result.push_back({path, skeletal.resolveMeshDataOnly(soe::MessageHash::compute(path),
            [&](const std::string& filename) { return materials.extract(filename); })});
    }
    return result;
}

void runVisualizer(soe::SoeSession& zoneSession, soe::MessageDispatcher& dispatcher, bool& failed,
                    swgproto::ObjControllerDispatcher& objControllerDispatcher,
                    const worldmodel::ObjectStore& objectStore, const std::string& clientPath,
                    const std::string& terrainName, int formula, int variant, int axes, int bindOnly, bool nativeClient,clientcommon::ChatLog* chatLog,clientcommon::CommandSubmission* commandSubmission,clientcommon::TellSubmission* tellSubmission) {
    (void)objControllerDispatcher;
    runVisualizerImpl(&zoneSession,&dispatcher,failed,&objectStore,clientPath,terrainName,formula,variant,axes,bindOnly,nullptr,nativeClient,chatLog,commandSubmission,tellSubmission);
}
void runOfflineVisualizer(const OfflineWorldOptions& options) {
    bool failed=false;
    runVisualizerImpl(nullptr,nullptr,failed,nullptr,options.clientPath,"terrain/tatooine.trn",0,0,0,0,&options);
    if(failed) throw std::runtime_error("Offline world failed");
}

void writeFrameAssetAudit(const std::string& clientPath,const std::string& output,bool ship) {
    RealMeshResolver assets(clientPath);RealBuildingResolver buildings(assets);
    const std::string path=ship?"object/ship/player/shared_player_yt1300.iff":"object/building/player/shared_player_guildhall_generic_style_01.iff";
    const auto crc=soe::MessageHash::compute(path);
    auto cells=buildings.resolveMeshDataOnly(crc);
    if(cells.size()!=(ship?8:18))throw std::runtime_error("Frame audit: unexpected parent layout");
    if(ship)for(size_t i=0;i<cells.size();++i)if(!cells[i].bounds||cells[i].submeshes.empty()||(i&&cells[i].floorMesh.positions.empty()))
        throw std::runtime_error("Ship audit: unresolved cell geometry/interior floor");
    size_t crossings=0,checks=0,oldFailures=0;float maxModelError=0,maxLocalError=0,maxWireError=0;
    for(float yaw:{.01373f,.7312345f,1.232345f,-2.12431f,3.14159265f}) {
        worldmodel::WorldObject building;building.objectId=200;building.transformMessagesSeen=1;building.fullRotationCurrent=true;
        building.x=-4807.123f;building.y=41.789f;building.z=2743.213f;
        auto q=worldmodel::headingRotation(yaw);building.quatY=q.y;building.quatW=q.w;
        // Deliberately retain a wrong direction byte: precise source must win.
        building.direction=0;auto frame=buildingFrame(building);auto model=spatialModelMatrix(frame);
        for(size_t cell=0;cell<cells.size();++cell)for(auto ref:cells[cell].portals) {
            const auto& authored=cells[cell].portalShapes.at(ref.portalShapeIndex);
            std::vector<assets::PortalShape> samples;
            if(authored.triangleIndices.empty())samples.push_back(authored);
            else for(size_t i=0;i<authored.triangleIndices.size();i+=3) {
                assets::PortalShape triangle;
                for(size_t j=0;j<3;++j)triangle.vertices.push_back(authored.vertices.at(authored.triangleIndices.at(i+j)));
                samples.push_back(std::move(triangle));
            }
            for(const auto& shape:samples) {
            assets::Float3 center{};
            for(auto v:shape.vertices){center.x+=v.x/shape.vertices.size();center.y+=v.y/shape.vertices.size();center.z+=v.z/shape.vertices.size();}
            auto a=shape.vertices.at(0),b=shape.vertices.at(1),c=shape.vertices.at(2);
            DirectX::XMVECTOR normal=DirectX::XMVector3Normalize(DirectX::XMVector3Cross(
                DirectX::XMVectorSet(b.x-a.x,b.y-a.y,b.z-a.z,0),DirectX::XMVectorSet(c.x-a.x,c.y-a.y,c.z-a.z,0)));
            DirectX::XMFLOAT3 n;DirectX::XMStoreFloat3(&n,normal);
            assets::Float3 from{center.x+n.x*.2f,center.y+n.y*.2f,center.z+n.z*.2f},to{center.x-n.x*.2f,center.y-n.y*.2f,center.z-n.z*.2f};
            // Independent analytic yaw, not the production quaternion transform.
            auto oracle=[&](assets::Float3 p){return assets::Float3{float(building.x+double(p.x)*std::cos(double(yaw))+double(p.z)*std::sin(double(yaw))),building.y+p.y,float(building.z-double(p.x)*std::sin(double(yaw))+double(p.z)*std::cos(double(yaw)))};};
            auto worldFrom=oracle(from),worldTo=oracle(to);
            DirectX::XMFLOAT3 gpuPoint;DirectX::XMStoreFloat3(&gpuPoint,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(from.x,from.y,from.z,1),DirectX::XMLoadFloat4x4(&model)));
            float modelError=std::hypot(std::hypot(gpuPoint.x-worldFrom.x,gpuPoint.z-worldFrom.z),gpuPoint.y-worldFrom.y);maxModelError=std::max(maxModelError,modelError);
            auto localFrom=frame.toLocal(worldFrom),localTo=frame.toLocal(worldTo);
            float localError=std::max(std::hypot(std::hypot(localFrom.x-from.x,localFrom.z-from.z),localFrom.y-from.y),std::hypot(std::hypot(localTo.x-to.x,localTo.z-to.z),localTo.y-to.y));maxLocalError=std::max(maxLocalError,localError);
            if(modelError>.002f||localError>.002f)throw std::runtime_error("Frame audit: model/local mismatch");checks+=2;
            auto rounded=building;rounded.fullRotationCurrent=false;rounded.direction=int8_t(int(yaw*100/DirectX::XM_2PI));
            auto old=buildingFrame(rounded).toWorld(from);
            if(std::hypot(old.x-worldFrom.x,old.z-worldFrom.z)>.002f)++oldFailures;
            worldmodel::CellTraversal traversal;traversal.select(building.objectId,cell);
            std::vector<assets::CellPortal> only{ref};auto crossing=traversal.propose(only,cells[cell].portalShapes,localFrom,localTo,cells.size());
            if(!crossing||crossing->destinationCell!=ref.adjacentCellIndex)throw std::runtime_error("Frame audit: exact doorway missed");++checks;++crossings;
            // These IDs are fixture-only. Exercise actual parented serializers.
            auto parentId=ref.adjacentCellIndex?uint64_t(70000+ref.adjacentCellIndex):0;
            clientcommon::MovementFrame movement{42,parentId,worldTo.x,worldTo.y,worldTo.z,yaw+.12345f,1.55f};
            clientcommon::MovementParentFrame parent{building.x,building.y,building.z,yaw,q};
            clientcommon::MovementReporter reporter(0);auto report=reporter.poll(400,movement,parent);
            if(!report)throw std::runtime_error("Frame audit: wire serialization failed");
            auto expected=parentId?to:worldTo;
            float wireError=std::hypot(std::hypot(report->frame.x-expected.x,report->frame.z-expected.z),report->frame.y-expected.y);maxWireError=std::max(maxWireError,wireError);
            if(wireError>.002f)throw std::runtime_error("Frame audit: wrong wire coordinate frame");++checks;
            }
        }
    }
    if(!crossings||oldFailures<crossings/2)throw std::runtime_error("Frame audit: retained quantization failure not reproduced");
    std::ofstream report(output);report.exceptions(std::ios::failbit|std::ios::badbit);
    report<<std::setprecision(9)<<"{\"passed\":true,\"server_contacted\":false,\"architecture\":\"arm64\",\"template\":\""<<path<<"\",\"template_crc\":"<<crc<<",\"cells\":"<<cells.size()<<",\"checks\":"<<checks<<",\"real_portal_crossings\":"<<crossings
        <<",\"retained_byte_yaw_failures_over_2mm\":"<<oldFailures<<",\"max_model_error_m\":"<<maxModelError<<",\"max_local_error_m\":"<<maxLocalError<<",\"max_wire_error_m\":"<<maxWireError
        <<",\"source\":\"real_TRE_with_synthetic_building_frames_and_server_ids\",\"original_controller_parity\":false}\n";
}

void writeCellAssetAudit(const std::string& clientPath,const std::string& output) {
    RealMeshResolver assets(clientPath);RealBuildingResolver buildings(assets);
    auto cells=buildings.resolveMeshDataOnly(soe::MessageHash::compute(
        "object/building/player/shared_player_guildhall_generic_style_01.iff"));
    if(cells.size()<2)throw std::runtime_error("Cell audit: no guildhall interior");
    size_t references=0,cases=0,transforms=0;float maxError=0;
    std::vector<worldmodel::ServerCellBinding> bindings;
    for(size_t i=1;i<cells.size();++i)bindings.push_back({99900+i*79,777,int32_t(i)});
    std::reverse(bindings.begin(),bindings.end());
    for(size_t cell=0;cell<cells.size();++cell)for(auto ref:cells[cell].portals) {
        if(ref.portalShapeIndex>=cells[cell].portalShapes.size()||ref.adjacentCellIndex>=cells.size())
            throw std::runtime_error("Cell audit: invalid real portal reference");
        const auto& shape=cells[cell].portalShapes[ref.portalShapeIndex];
        if(shape.vertices.size()<3)throw std::runtime_error("Cell audit: degenerate real portal");
        assets::Float3 center{};for(auto v:shape.vertices){center.x+=v.x/shape.vertices.size();center.y+=v.y/shape.vertices.size();center.z+=v.z/shape.vertices.size();}
        // Independent triangle cross product, rather than production Newell normal.
        auto a=shape.vertices[0],b=shape.vertices[1],c=shape.vertices[2];
        double nx=(double(b.y)-a.y)*(double(c.z)-a.z)-(double(b.z)-a.z)*(double(c.y)-a.y);
        double ny=(double(b.z)-a.z)*(double(c.x)-a.x)-(double(b.x)-a.x)*(double(c.z)-a.z);
        double nz=(double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x);
        double length=std::sqrt(nx*nx+ny*ny+nz*nz);if(length<1e-8)throw std::runtime_error("Cell audit: invalid portal plane");
        nx/=length;ny/=length;nz/=length;
        for(auto v:shape.vertices)if(std::abs(nx*(v.x-a.x)+ny*(v.y-a.y)+nz*(v.z-a.z))>.002)
            throw std::runtime_error("Cell audit: nonplanar real aperture");
        bool reciprocal=false;for(auto other:cells[ref.adjacentCellIndex].portals)
            if(other.adjacentCellIndex==cell&&other.portalShapeIndex==ref.portalShapeIndex)reciprocal=true;
        if(!reciprocal)throw std::runtime_error("Cell audit: nonreciprocal real portal graph");
        auto offset=[&](float distance){return assets::Float3{center.x+float(nx*distance),center.y+float(ny*distance),center.z+float(nz*distance)};};
        std::vector<assets::CellPortal> only{ref};
        for(int direction:{0,13,23,50,87})for(int sign:{-1,1}) {
            auto from=offset(.2f*sign),to=offset(-.2f*sign);
            worldmodel::WorldObject building;building.transformMessagesSeen=1;building.x=-4800;building.y=42;building.z=2700;building.direction=direction;
            auto wfrom=buildingLocalToWorld({from.x,from.y,from.z},building),wto=buildingLocalToWorld({to.x,to.y,to.z},building);
            auto f=worldToBuildingLocal(wfrom,building),t=worldToBuildingLocal(wto,building);
            float error=std::max(std::hypot(f.x-from.x,f.z-from.z),std::hypot(t.x-to.x,t.z-to.z));maxError=std::max(maxError,error);
            if(error>.002)throw std::runtime_error("Cell audit: transformed doorway error");++transforms;
            worldmodel::CellTraversal traversal;traversal.select(777,cell);
            auto proposal=traversal.propose(only,cells[cell].portalShapes,{f.x,f.y,f.z},{t.x,t.y,t.z},cells.size());
            if(!proposal||proposal->destinationCell!=ref.adjacentCellIndex||std::abs(proposal->fraction-.5f)>.003f)
                throw std::runtime_error("Cell audit: real aperture crossing missed");++cases;
            if(traversal.commit(*proposal,false)||traversal.cell()!=cell)throw std::runtime_error("Cell audit: rejected crossing changed state");++cases;
            if(!worldmodel::resolveCellParent(bindings,777,proposal->destinationCell))throw std::runtime_error("Cell audit: shuffled mapping failed");++cases;
            if(!traversal.commit(*proposal,true)||traversal.cell()!=ref.adjacentCellIndex)throw std::runtime_error("Cell audit: accepted crossing failed");++cases;
        }
        worldmodel::CellTraversal traversal;traversal.select(777,cell);
        if(traversal.propose(only,cells[cell].portalShapes,offset(.2f),center,cells.size()))throw std::runtime_error("Cell audit: boundary committed prematurely");++cases;
        auto pending=traversal.propose(only,cells[cell].portalShapes,center,offset(-.2f),cells.size());
        if(!pending||pending->fraction!=0)throw std::runtime_error("Cell audit: boundary crossing lost");++cases;
        ++references;
    }
    if(references<20)throw std::runtime_error("Cell audit: insufficient real aperture coverage");
    std::ofstream report(output);report.exceptions(std::ios::failbit|std::ios::badbit);
    report<<std::setprecision(9)<<"{\"passed\":true,\"architecture\":\"arm64\",\"server_contacted\":false,\"cells\":"<<cells.size()
        <<",\"portal_references\":"<<references<<",\"checks\":"<<cases<<",\"transformed_crossings\":"<<transforms
        <<",\"max_transform_error_m\":"<<maxError<<",\"server_mapping_fixture\":\"synthetic_shuffled_ids\",\"original_controller_parity\":false}\n";
}

void writeFloorAssetAudit(const std::string& clientPath,const std::string& output) {
    RealMeshResolver assets(clientPath);RealBuildingResolver buildings(assets);
    auto cells=buildings.resolveMeshDataOnly(soe::MessageHash::compute(
        "object/building/player/shared_player_guildhall_generic_style_01.iff"));
    if(cells.size()<2)throw std::runtime_error("Floor audit: missing guildhall cells");
    const auto& shell=cells.front();
    auto baseline=queryFloorHeight2DFullScan(shell.floorMesh,{0,0,36.5f},0);
    auto fixed=worldmodel::queryExteriorFloorSupport(shell.floorMesh,shell.collisionMesh,{0,0,36.5f},0);
    if(!baseline||std::abs(baseline->y+15.5f)>.002f||fixed.height!=0||!fixed.buriedCandidates)
        throw std::runtime_error("Floor audit: retained failure not reproduced and corrected");
    size_t cases=0,interiorCases=0,rampCases=0,terrainCases=0,transformCases=0;
    float maxHeightError=0,maxTransformError=0;
    // Independent plane intersection and signed edge oracle, using exact
    // coverage (no production barycentric tolerance or support selection).
    auto oracle=[&](const assets::FloorCollisionMesh& mesh,DirectX::XMFLOAT3 p)->std::optional<float> {
        std::optional<float> best;float delta=1e30f;
        for(size_t i=0;i+2<mesh.triangleVertexIndices.size();i+=3) {
            const auto& a=mesh.positions.at(mesh.triangleVertexIndices[i]);
            const auto& b=mesh.positions.at(mesh.triangleVertexIndices[i+1]);
            const auto& c=mesh.positions.at(mesh.triangleVertexIndices[i+2]);
            double nx=(double(b.y)-a.y)*(double(c.z)-a.z)-(double(b.z)-a.z)*(double(c.y)-a.y);
            double ny=(double(b.z)-a.z)*(double(c.x)-a.x)-(double(b.x)-a.x)*(double(c.z)-a.z);
            double nz=(double(b.x)-a.x)*(double(c.y)-a.y)-(double(b.y)-a.y)*(double(c.x)-a.x);
            if(std::abs(ny)<1e-9)continue;
            auto edge=[&](assets::Float3 u,assets::Float3 v){return (double(v.x)-u.x)*(double(p.z)-u.z)-(double(v.z)-u.z)*(double(p.x)-u.x);};
            double e0=edge(a,b),e1=edge(b,c),e2=edge(c,a);
            if(!((e0>=-1e-9&&e1>=-1e-9&&e2>=-1e-9)||(e0<=1e-9&&e1<=1e-9&&e2<=1e-9)))continue;
            float y=a.y-float((nx*(double(p.x)-a.x)+nz*(double(p.z)-a.z))/ny);
            if(std::abs(y-p.y)<delta){delta=std::abs(y-p.y);best=y;}
        }return best;
    };
    // Authored ramp sample centroids avoid ambiguous shared boundary edges.
    for(size_t triangle:{size_t(14),size_t(15),size_t(18),size_t(27),size_t(28)}) {
        assets::Float3 p{};
        for(int k=0;k<3;++k){auto v=shell.floorMesh.positions.at(shell.floorMesh.triangleVertexIndices.at(triangle*3+k));p.x+=v.x/3;p.y+=v.y/3;p.z+=v.z/3;}
        auto expected=oracle(shell.floorMesh,{p.x,p.y,p.z});
        auto hit=worldmodel::queryExteriorFloorSupport(shell.floorMesh,shell.collisionMesh,p,0);
        if(!expected||hit.source!=worldmodel::ExteriorFloorSupport::Source::FloorMesh)throw std::runtime_error("Floor audit: missed real ramp");
        float error=std::abs(hit.height-*expected);maxHeightError=std::max(maxHeightError,error);
        if(error>.002f)throw std::runtime_error("Floor audit: independent real ramp height mismatch");
        ++cases;++rampCases;
        for(int direction:{0,13,23,50,87}) {
            worldmodel::WorldObject building;building.transformMessagesSeen=1;building.x=-2736;building.y=5;building.z=2136;building.direction=direction;
            auto world=buildingLocalToWorld({p.x,p.y,p.z},building);
            auto local=worldToBuildingLocal(world,building);
            auto transformed=worldmodel::queryExteriorFloorSupport(shell.floorMesh,shell.collisionMesh,{local.x,local.y,local.z},0);
            float error=std::abs(building.y+transformed.height-(5+hit.height));maxTransformError=std::max(maxTransformError,error);
            if(error>.002f)throw std::runtime_error("Floor audit: rotated/translated support mismatch");
            ++transformCases;++cases;
        }
    }
    for(float z:{65.f,52.5f,44.f,40.f,39.f,38.f,36.5f,34.f,32.5f,31.f,30.f})for(float x:{-2.f,0.f,2.f}) {
        auto hit=worldmodel::queryExteriorFloorSupport(shell.floorMesh,shell.collisionMesh,{x,0,z},0);
        if(hit.height!=0||hit.source!=worldmodel::ExteriorFloorSupport::Source::Terrain)
            throw std::runtime_error("Floor audit: exterior approach did not remain on terrain");
        ++cases;++terrainCases;
    }
    // Interior calls deliberately use the existing floor query rather than
    // exterior terrain support. Verify buried and upper room centroids against
    // the independent plane oracle, without assigning a new metadata meaning.
    for(size_t cell=1;cell<cells.size();++cell) {
        const auto& f=cells[cell].floorMesh;
        for(size_t i=0;i+2<f.triangleVertexIndices.size();i+=3) {
            DirectX::XMFLOAT3 p{};
            for(int k=0;k<3;++k){auto v=f.positions.at(f.triangleVertexIndices[i+k]);p.x+=v.x/3;p.y+=v.y/3;p.z+=v.z/3;}
            auto expected=oracle(f,p);if(!expected)continue;
            auto hit=queryFloorHeight2DFullScan(f,p,p.y);
            if(!hit||std::abs(hit->y-*expected)>.002f)throw std::runtime_error("Floor audit: interior floor regression");
            maxHeightError=std::max(maxHeightError,std::abs(hit->y-*expected));++cases;++interiorCases;
        }
    }
    if(interiorCases<50)throw std::runtime_error("Floor audit: insufficient interior coverage");
    std::ofstream report(output);report.exceptions(std::ios::failbit|std::ios::badbit);report<<std::setprecision(9);
    report<<"{\"passed\":true,\"server_contacted\":false,\"architecture\":\"arm64\",\"cases\":"<<cases
        <<",\"ramp_cases\":"<<rampCases<<",\"approach_cases\":"<<terrainCases<<",\"interior_cases\":"<<interiorCases
        <<",\"transformed_building_cases\":"<<transformCases<<",\"max_height_error\":"<<maxHeightError<<",\"max_transform_height_error\":"<<maxTransformError
        <<",\"retained_failure\":{\"local_x\":0,\"local_z\":36.5,\"triangle\":"<<baseline->triangleIndex<<",\"old_local_height\":"<<baseline->y
        <<",\"fixed_local_height\":"<<fixed.height<<",\"source\":\"terrain\"},\"original_controller_parity\":false}\n";
}

void writeCameraAssetAudit(const std::string& clientPath,const std::string& output) {
    RealMeshResolver assets(clientPath);RealBuildingResolver buildings(assets);
    auto cells=buildings.resolveMeshDataOnly(soe::MessageHash::compute(
        "object/building/player/shared_player_guildhall_generic_style_01.iff"));
    TerrainResolver terrains(assets);auto terrain=terrains.resolve("terrain/tatooine.trn");
    if(!terrain)throw std::runtime_error("Camera audit: missing Tatooine");
    size_t cases=0,overlaps=0,triangles=0,terrainCases=0,transforms=0,roofCases=0;
    float minimumClearance=100,maxTransformError=0;uint64_t testedTriangles=0;
    const float radius=renderer::cameraClearanceRadius(DirectX::XM_PIDIV4,1000.f/740,.1f);
    auto check=[&](const assets::MeshData& mesh,const renderer::CameraCollisionMesh& camera,bool isTerrain) {
        triangles+=mesh.indices.size()/3;
        auto distance=[&](assets::Float3 p) {
            float nearest=1e30f;
            for(size_t j=0;j+2<mesh.indices.size();j+=3) {
                const auto& a=mesh.positions[mesh.indices[j]];const auto& b=mesh.positions[mesh.indices[j+1]];const auto& c=mesh.positions[mesh.indices[j+2]];
                auto n=DirectX::XMVector3Cross(DirectX::XMVectorSet(b.x-a.x,b.y-a.y,b.z-a.z,0),DirectX::XMVectorSet(c.x-a.x,c.y-a.y,c.z-a.z,0));
                if(DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n))<1e-12f)continue;
                auto q=closestPointOnTriangle3D({p.x,p.y,p.z},a,b,c);
                nearest=std::min(nearest,std::sqrt((p.x-q.x)*(p.x-q.x)+(p.y-q.y)*(p.y-q.y)+(p.z-q.z)*(p.z-q.z)));
            }return nearest;
        };
        size_t stride=std::max<size_t>(1,mesh.indices.size()/3/48);
        for(size_t j=0;j+2<mesh.indices.size();j+=stride*3) {
            const auto& a=mesh.positions[mesh.indices[j]];const auto& b=mesh.positions[mesh.indices[j+1]];const auto& c=mesh.positions[mesh.indices[j+2]];
            auto cross=DirectX::XMVector3Cross(DirectX::XMVectorSet(b.x-a.x,b.y-a.y,b.z-a.z,0),DirectX::XMVectorSet(c.x-a.x,c.y-a.y,c.z-a.z,0));
            if(DirectX::XMVectorGetX(DirectX::XMVector3Length(cross))<.01f)continue;
            DirectX::XMFLOAT3 n;DirectX::XMStoreFloat3(&n,DirectX::XMVector3Normalize(cross));
            assets::Float3 p{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};
            assets::Float3 from{p.x+n.x,p.y+n.y,p.z+n.z},to{p.x-n.x,p.y-n.y,p.z-n.z};
            auto hit=camera.sweep(from,to,radius);testedTriangles+=hit.triangleTests;
            if(hit.initialOverlap){++overlaps;continue;}
            if(!hit.hit||hit.fraction>.5f)throw std::runtime_error("Camera audit: missed real surface");
            assets::Float3 end{from.x+(to.x-from.x)*hit.fraction,from.y+(to.y-from.y)*hit.fraction,from.z+(to.z-from.z)*hit.fraction};
            float clearance=distance(end);minimumClearance=std::min(minimumClearance,clearance);
            if(!std::isfinite(clearance)||clearance<radius-.002f||clearance>radius+.003f)
                throw std::runtime_error("Camera audit: independent closest-point contact check failed");
            // Check the whole safe segment with an independently implemented
            // closest-point oracle, not another call to the sweep solver.
            for(int sample=0;sample<32;++sample) {
                float t=hit.fraction*float(sample)/32;
                assets::Float3 point{from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};
                if(distance(point)<radius-.002f)throw std::runtime_error("Camera audit: crossed geometry before reported contact");
            }
            ++cases;if(isTerrain)++terrainCases;else if(std::abs(n.y)>.85f)++roofCases;
            if(!isTerrain) {
                // Full world-space mesh query versus production inverse transform,
                // with a nonzero yaw and large translation.
                worldmodel::WorldObject building;building.transformMessagesSeen=1;building.x=-2736;building.y=17;building.z=2136;building.direction=23;
                auto worldFrom=buildingLocalToWorld({from.x,from.y,from.z},building),worldTo=buildingLocalToWorld({to.x,to.y,to.z},building);
                auto localFrom=worldToBuildingLocal(worldFrom,building),localTo=worldToBuildingLocal(worldTo,building);
                auto localHit=camera.sweep({localFrom.x,localFrom.y,localFrom.z},{localTo.x,localTo.y,localTo.z},radius);
                // Independent renderer matrix convention (row-vector Y rotation).
                auto model=spatialModelMatrix(buildingFrame(building));
                auto matrix=DirectX::XMLoadFloat4x4(&model);
                std::vector<assets::Float3> worldVertices;worldVertices.reserve(mesh.positions.size());
                for(auto vertex:mesh.positions){DirectX::XMFLOAT3 w;DirectX::XMStoreFloat3(&w,DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(vertex.x,vertex.y,vertex.z,1),matrix));worldVertices.push_back({w.x,w.y,w.z});}
                renderer::CameraCollisionMesh worldMesh(worldVertices,mesh.indices);
                auto worldHit=worldMesh.sweep({worldFrom.x,worldFrom.y,worldFrom.z},{worldTo.x,worldTo.y,worldTo.z},radius);
                // World-space float quantization at ~3km changes grazing
                // contact distance more than coordinate roundoff itself.
                // Permit 1cm along the boom (below the extra 5cm clearance),
                // and retain the actual measured maximum in the report.
                float error=std::abs(worldHit.fraction-localHit.fraction)*2;
                maxTransformError=std::max(maxTransformError,error);
                if(worldHit.hit!=localHit.hit||worldHit.initialOverlap!=localHit.initialOverlap||error>.01f)
                    throw std::runtime_error("Camera audit: transformed building mismatch index="+std::to_string(j)+" local="+std::to_string(localHit.fraction)+" world="+std::to_string(worldHit.fraction)+" overlaps="+std::to_string(localHit.initialOverlap)+","+std::to_string(worldHit.initialOverlap)+" error="+std::to_string(error));
                ++transforms;
            }
        }
    };
    for(const auto& cell:cells) {
        assets::MeshData mesh;
        for(const auto& part:cell.submeshes){auto base=uint32_t(mesh.positions.size());mesh.positions.insert(mesh.positions.end(),part.mesh.positions.begin(),part.mesh.positions.end());for(auto i:part.mesh.indices)mesh.indices.push_back(base+i);}
        if(cell.cameraMesh&&!mesh.indices.empty())check(mesh,*cell.cameraMesh,false);
    }
    for(auto coord: {terrain::ChunkCoord{-22,16},terrain::ChunkCoord{-21,16},terrain::ChunkCoord{0,0}}) {
        auto generated=terrain::buildChunkMesh(terrain->generateChunk(coord));assets::MeshData mesh;
        for(const auto& vertex:generated.vertices)mesh.positions.push_back(vertex.position);mesh.indices=std::move(generated.indices);
        renderer::CameraCollisionMesh camera(mesh.positions,mesh.indices);check(mesh,camera,true);
    }
    if(cases<50||terrainCases<30||transforms<20||roofCases<5)throw std::runtime_error("Camera audit: insufficient real geometry coverage");
    std::ofstream report(output);report.exceptions(std::ios::failbit|std::ios::badbit);report<<std::setprecision(9);
    report<<"{\"passed\":true,\"server_contacted\":false,\"architecture\":\"arm64\",\"building\":\"shared_player_guildhall_generic_style_01\",\"terrain\":\"tatooine\",\"cases\":"<<cases
        <<",\"terrain_cases\":"<<terrainCases<<",\"horizontal_building_surface_cases\":"<<roofCases<<",\"transformed_building_cases\":"<<transforms
        <<",\"initial_overlaps_excluded\":"<<overlaps<<",\"triangles\":"<<triangles<<",\"sweep_triangle_tests\":"<<testedTriangles
        <<",\"radius\":"<<radius<<",\"minimum_contact_clearance\":"<<minimumClearance<<",\"max_transform_distance_error\":"<<maxTransformError
        <<",\"oracle\":\"independent closest-point contact plus 32 safe-segment samples per case\",\"original_camera_parity\":false}\n";
}

#endif
