add_executable(dummyclient main.cpp)
target_link_libraries(dummyclient PRIVATE soe::soe swgproto::swgproto clientcommon::clientcommon worldmodel::worldmodel)
if(SWG_BUILD_RENDERER)
    add_library(clientvisualizer STATIC Visualizer.cpp PngWriter.cpp
        AnimationDebugControls.cpp ScreenshotCapture.cpp RestPoseAutoTest.cpp
        AnimationDiagnosticLogging.cpp ClientArchiveOrder.cpp MaterialResolver.cpp TargetHud.cpp OriginalHudAssets.cpp CommandCatalog.cpp LocalizedChat.cpp TemplateGender.cpp TellCommands.cpp)
    target_compile_definitions(clientvisualizer PUBLIC SWG_HAS_VISUALIZER)
    target_include_directories(clientvisualizer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(clientvisualizer PUBLIC renderer::renderer assets::assets terrain::terrain
        animation::animation creatureanim::creatureanim clientcommon::clientcommon soe::soe swgproto::swgproto worldmodel::worldmodel)
    target_link_libraries(dummyclient PRIVATE clientvisualizer)
endif()
