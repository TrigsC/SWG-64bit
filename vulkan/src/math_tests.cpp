#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <renderer/Camera.h>
#include <animation/SkeletalPose.h>
#if defined(__aarch64__) && (!defined(_XM_ARM_NEON_INTRINSICS_) || defined(_XM_NO_INTRINSICS_))
#error ARM64 math must use the NEON implementation
#endif
using namespace DirectX;
TEST_CASE("ARM64 camera transform keeps the target on screen") {
    renderer::FlyCamera c;c.position={0,0,-5};c.pitch=0;c.yaw=0;
    XMFLOAT3 result;XMStoreFloat3(&result,XMVector3TransformCoord(XMVectorSet(0,0,0,1),c.viewMatrix()));
    CHECK(result.x==doctest::Approx(0));CHECK(result.y==doctest::Approx(0));CHECK(result.z==doctest::Approx(5));
}
TEST_CASE("ARM64 bone hierarchy composes parent and child translations") {
    assets::SkeletonData skeleton;assets::SkeletonBone root;root.name="root";root.bindTranslation={3,0,0};
    assets::SkeletonBone child;child.name="child";child.parentIndex=0;child.bindTranslation={0,2,0};skeleton.bones={root,child};
    auto local=animation::sampleLocalBoneTransforms(skeleton,nullptr,{},0);
    auto world=animation::computeWorldBoneTransforms(skeleton,local);
    REQUIRE(world.size()==2);XMFLOAT3 point;XMStoreFloat3(&point,XMVector3TransformCoord(XMVectorZero(),world[1]));
    CHECK(point.x==doctest::Approx(3));CHECK(point.y==doctest::Approx(2));CHECK(point.z==doctest::Approx(0));
    auto identity=world[1]*XMMatrixInverse(nullptr,world[1]);XMFLOAT4X4 matrix;XMStoreFloat4x4(&matrix,identity);
    CHECK(matrix._11==doctest::Approx(1));CHECK(matrix._22==doctest::Approx(1));CHECK(matrix._33==doctest::Approx(1));CHECK(matrix._44==doctest::Approx(1));
}
TEST_CASE("ARM64 quaternion interpolation and weighted skinning rotate positions and normals") {
    auto q=XMQuaternionSlerp(XMQuaternionIdentity(),XMQuaternionRotationAxis(XMVectorSet(0,0,1,0),XM_PIDIV2),0.5f);
    XMFLOAT3 rotated;XMStoreFloat3(&rotated,XMVector3Rotate(XMVectorSet(1,0,0,0),q));
    CHECK(rotated.x==doctest::Approx(std::sqrt(0.5f)).epsilon(0.0001));
    CHECK(rotated.y==doctest::Approx(std::sqrt(0.5f)).epsilon(0.0001));
    assets::SkeletonBone root;root.name="root";assets::SkeletonData skeleton;skeleton.bones={root};
    assets::SkeletalMeshSubmesh mesh;mesh.positions={{2,0,0}};mesh.normals={{1,0,0}};mesh.sourceVertexIndices={0};
    std::vector<assets::Float3> positions,normals;
    animation::skinSubmeshVertices(mesh,{{{0,1}}},skeleton,{0},{"root"},{XMMatrixRotationZ(XM_PIDIV2)},positions,normals);
    REQUIRE(positions.size()==1);REQUIRE(normals.size()==1);
    CHECK(positions[0].x==doctest::Approx(0).epsilon(0.0001));CHECK(positions[0].y==doctest::Approx(2));
    CHECK(normals[0].x==doctest::Approx(0).epsilon(0.0001));CHECK(normals[0].y==doctest::Approx(1));
}
