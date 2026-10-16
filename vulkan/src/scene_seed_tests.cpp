#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <limits>
#include <vector>
#include "clientcommon/TargetSelection.h"
#include "soe/PacketBuffer.h"
#include "swgproto/ObjectStateDispatcher.h"
#include "swgproto/UpdateTransformMessage.h"
#include "worldmodel/ObjectStore.h"
#include "worldmodel/SpatialTransform.h"

namespace {
void staticBaseline(swgproto::ObjectStateDispatcher& dispatcher, uint64_t id) {
    soe::PacketBuffer packet;
    packet.writeUint64(id);
    packet.writeUint32(0x4f415453); // OATS
    packet.writeByte(3);
    packet.writeUint32(2); // count field; dispatcher does not use payload size
    packet.writeUint16(4);
    packet.writeUint32(0);
    packet.writeAscii("object/static");
    packet.writeUint32(0);
    packet.writeAscii("test");
    packet.writeUnicode(u"Stationary object");
    packet.writeUint32(0xff);
    REQUIRE(dispatcher.dispatchBaseline(packet));
}

std::optional<worldmodel::SpatialTransform> pose(const worldmodel::ObjectStore& store,
                                                 uint64_t id) {
    auto snapshot = store.snapshotWorldObjects();
    auto object = snapshot.find(id);
    if (object == snapshot.end() || !object->second.baselineMessagesSeen) return {};
    return worldmodel::resolveWorldTransform(snapshot, object->second);
}
}

TEST_CASE("Scene-create seed materializes a stationary baseline and can be selected") {
    worldmodel::ObjectStore store(0);
    swgproto::ObjectStateDispatcher dispatcher;
    store.registerHandlers(dispatcher);
    constexpr uint64_t id = 0x100000042;
    store.seedPosition(id, 12, 3, 24, 0, 0, 0, 1, 0xabcdef01);
    CHECK(store.pendingPositionSeedCount() == 1);
    CHECK_FALSE(pose(store, id)); // scene create alone is not a usable object
    staticBaseline(dispatcher, id);
    CHECK(store.pendingPositionSeedCount() == 0);
    auto object = store.findAs<worldmodel::StaticObject>(id);
    REQUIRE(object);
    CHECK(object->baselineMessagesSeen == 1);
    CHECK(object->objectCrc == 0xabcdef01);
    auto resolved = pose(store, id);
    REQUIRE(resolved);
    CHECK(resolved->origin.x == 12);
    CHECK(resolved->origin.y == 3);
    CHECK(resolved->origin.z == 24);
    clientcommon::TargetSelection selection;
    const std::vector<clientcommon::InspectionTarget> candidates{
        {id, u"Stationary object", {12, 3, 24}, true, false}};
    selection.pick(candidates, {}, {12, 3, 0}, {0, 0, 1});
    CHECK(selection.id() == id);

    swgproto::UpdateTransformMessage moved;
    moved.objectId = id;
    moved.x = 18;
    moved.y = 4;
    moved.z = 30;
    store.applyTransform(moved);
    store.seedPosition(id, 12, 3, 24, 0, 0, 0, 1, 0xabcdef01);
    resolved = pose(store, id);
    REQUIRE(resolved);
    CHECK(resolved->origin.x == 18); // late scene create must not rewind movement
    store.remove(id);
    CHECK_FALSE(pose(store, id));
    CHECK(store.size() == 0);
}

TEST_CASE("Invalid scene-create positions and rotations never become renderable") {
    worldmodel::ObjectStore store(0);
    swgproto::ObjectStateDispatcher dispatcher;
    store.registerHandlers(dispatcher);
    constexpr uint64_t id = 77;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    store.seedPosition(id, nan, 0, 0, 0, 0, 0, 1, 5);
    store.seedPosition(id, 0, 0, 0, 0, 0, 0, 0, 5);
    CHECK(store.pendingPositionSeedCount() == 0);
    staticBaseline(dispatcher, id);
    CHECK_FALSE(pose(store, id));
    store.seedPosition(id, 7, 2, 9, 0, 0, 0, 1, 5);
    REQUIRE(pose(store, id));
    store.seedPosition(id, nan, 0, 0, 0, 0, 0, 1, 5);
    CHECK(pose(store, id)->origin.x == 7);
    store.remove(id);
    store.seedPosition(id, 7, 2, 9, 0, 0, 0, 1, 5);
    CHECK_FALSE(pose(store, id));
    CHECK(store.pendingPositionSeedCount() == 1);
    store.clear();
    CHECK(store.pendingPositionSeedCount() == 0);
}
