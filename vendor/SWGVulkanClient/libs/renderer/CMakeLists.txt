# Vulkan renderer. Windows retains native Win32 windowing and DXC/HLSL.
# macOS uses SDL3 surfaces/input, CoreText labels, and glslang/GLSL.
# Both paths share resource management, draw pipelines, and camera math.
add_library(renderer STATIC
    src/VulkanRenderer.cpp
    src/Camera.cpp
    src/CameraCollision.cpp
)
add_library(renderer::renderer ALIAS renderer)

target_include_directories(renderer PUBLIC include)
target_include_directories(renderer SYSTEM PUBLIC "${vma_SOURCE_DIR}/include")
target_link_libraries(renderer PUBLIC Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator assets::assets terrain::terrain swg::math)
if(WIN32)
    target_sources(renderer PRIVATE src/Window.cpp)
    target_compile_definitions(renderer PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(renderer PRIVATE gdi32)
else()
    target_sources(renderer PRIVATE src/WindowSDL.cpp)
    target_compile_definitions(renderer PRIVATE SWG_VULKAN_LIBRARY_PATH="${Vulkan_LIBRARY}")
    target_link_libraries(renderer PUBLIC SDL3::SDL3)
    if(APPLE)
        target_sources(renderer PRIVATE src/TextRasterizerApple.cpp)
        target_link_libraries(renderer PRIVATE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
    else()
        message(FATAL_ERROR "Portable text rasterization is currently implemented for macOS only")
    endif()
endif()

# Shaders stay HLSL (kept from the D3D11 pass almost unchanged) but compile
# to SPIR-V via DXC (bundled with the Vulkan SDK) instead of D3DCompile at
# build time rather than runtime, so a shader typo is a build failure, not a
# runtime exception in the middle of a live visualizer session.
if(WIN32)
    find_program(DXC_EXECUTABLE dxc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
else()
    find_program(GLSLANG_EXECUTABLE NAMES glslang glslangValidator REQUIRED)
    target_compile_definitions(renderer PRIVATE SWG_GLSL_SHADERS=1)
endif()

function(swg_add_hlsl_shader TARGET_NAME SHADER_FILE PROFILE ENTRY)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    set(SPIRV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}_${ENTRY}.spv")
    if(NOT WIN32)
        if(ENTRY STREQUAL "VSMain")
            set(STAGE vert)
        else()
            set(STAGE frag)
        endif()
        set(GLSL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glsl/${SHADER_NAME}.${STAGE}")
        add_custom_command(OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${GLSLANG_EXECUTABLE} -V --target-env vulkan1.2 -o ${SPIRV_OUTPUT} ${GLSL_FILE}
            DEPENDS ${GLSL_FILE} COMMENT "Compiling ${GLSL_FILE} to SPIR-V" VERBATIM)
        target_sources(${TARGET_NAME} PRIVATE ${SPIRV_OUTPUT})
        return()
    endif()
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND ${DXC_EXECUTABLE} -spirv -T ${PROFILE} -E ${ENTRY} -Fo ${SPIRV_OUTPUT} ${SHADER_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} (${ENTRY}) to SPIR-V"
        VERBATIM
    )
    target_sources(${TARGET_NAME} PRIVATE ${SPIRV_OUTPUT})
endfunction()

swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Wireframe.hlsl" vs_6_0 VSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Wireframe.hlsl" ps_6_0 PSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Label.hlsl" vs_6_0 VSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Label.hlsl" ps_6_0 PSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl" vs_6_0 VSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Mesh.hlsl" ps_6_0 PSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Terrain.hlsl" vs_6_0 VSMain)
swg_add_hlsl_shader(renderer "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Terrain.hlsl" ps_6_0 PSMain)

target_compile_definitions(renderer PRIVATE SWG_SHADER_SPV_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
