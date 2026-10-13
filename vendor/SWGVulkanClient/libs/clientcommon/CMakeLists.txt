add_library(clientcommon STATIC
    src/LoginInput.cpp
    src/MovementReporting.cpp
    src/RemoteMotion.cpp
    src/TargetSelection.cpp
    src/HexDump.cpp
    src/ObjControllerHandlers.cpp
)
add_library(clientcommon::clientcommon ALIAS clientcommon)

target_include_directories(clientcommon PUBLIC include)
target_link_libraries(clientcommon PUBLIC soe::soe swgproto::swgproto worldmodel::worldmodel)
