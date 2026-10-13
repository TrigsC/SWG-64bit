add_library(assets STATIC
    src/IffReader.cpp
    src/TreArchive.cpp
    src/SharedObjectTemplate.cpp
    src/AppearanceTemplate.cpp
    src/LodFile.cpp
    src/StaticMesh.cpp
    src/TgaImage.cpp
    src/Skeleton.cpp
    src/SkeletalMeshLod.cpp
    src/SkeletalMesh.cpp
    src/SkeletalAppearance.cpp
    src/AnimationStateTable.cpp
    src/AnimationPriorityMap.cpp
    src/AnimationActionMap.cpp
    src/AnimationClip.cpp
    src/BuildingLayout.cpp
    src/ShaderTemplate.cpp
    src/DdsImage.cpp
)
add_library(assets::assets ALIAS assets)

target_include_directories(assets PUBLIC include)
# The community setup generates an object-template index from each user's TRE
# filename tables under ignored tools/local-assets. Standalone upstream builds
# may continue to supply their own assets data directory.
if(NOT DEFINED SWG_ASSETS_DATA_DIR)
    set(SWG_ASSETS_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()
target_compile_definitions(assets PUBLIC SWG_ASSETS_DATA_DIR="${SWG_ASSETS_DATA_DIR}")
# swg::zlib: TreArchive decompresses its record/filename tables (zlib, same
# as this project already depends on for SOE packet compression - see
# libs/soe/src/Compression.cpp). soe::soe: only for soe/MessageHash.h
# (header-only) and soe/PacketBuffer.h, reused here as the chunk byte reader
# rather than inventing a second one.
target_link_libraries(assets PUBLIC swg::zlib soe::soe)
