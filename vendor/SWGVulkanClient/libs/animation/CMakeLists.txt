# Pure CPU skeletal math. swg::math supplies Windows SDK DirectXMath or
# the pinned portable library (tested with ARM64 NEON on macOS).
add_library(animation STATIC
    src/SkeletalPose.cpp
    src/LocomotionPlayback.cpp
    src/ActionPlayback.cpp
)
add_library(animation::animation ALIAS animation)

target_include_directories(animation PUBLIC include)
target_link_libraries(animation PUBLIC assets::assets swg::math)
