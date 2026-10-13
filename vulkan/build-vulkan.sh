#!/bin/bash
set -euo pipefail
vulkan_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$vulkan_dir")"
cmake_bin="$modern_dir/tools/python/bin/cmake"
ninja_bin="$modern_dir/tools/python/bin/ninja"
deps="$modern_dir/tools/vulkan"
reports="$modern_dir/reports"
trap 'echo "Build failed. See logs under $reports." >&2' ERR
common=(-G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja_bin" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release)
"$cmake_bin" -S "$deps/sdl/SDL3-3.4.18" -B "$modern_dir/build/sdl3" "${common[@]}" \
    "-DCMAKE_INSTALL_PREFIX=$deps/sdl-install" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_SHARED=ON -DSDL_STATIC=OFF > "$reports/sdl3-configure.log" 2>&1
"$cmake_bin" --build "$modern_dir/build/sdl3" --parallel 4 > "$reports/sdl3-build.log" 2>&1
"$cmake_bin" --install "$modern_dir/build/sdl3" > "$reports/sdl3-install.log" 2>&1
"$cmake_bin" -S "$deps/Vulkan-Headers" -B "$modern_dir/build/vulkan-headers" "${common[@]}" \
    "-DCMAKE_INSTALL_PREFIX=$deps/headers-install" -DVULKAN_HEADERS_ENABLE_TESTS=OFF > "$reports/vulkan-headers-configure.log" 2>&1
"$cmake_bin" --install "$modern_dir/build/vulkan-headers" > "$reports/vulkan-headers-install.log" 2>&1
"$cmake_bin" -S "$deps/Vulkan-Loader" -B "$modern_dir/build/vulkan-loader" "${common[@]}" \
    "-DCMAKE_PREFIX_PATH=$deps/headers-install" "-DCMAKE_INSTALL_PREFIX=$deps/loader-install" -DBUILD_TESTS=OFF > "$reports/vulkan-loader-configure.log" 2>&1
"$cmake_bin" --build "$modern_dir/build/vulkan-loader" --parallel 4 > "$reports/vulkan-loader-build.log" 2>&1
"$cmake_bin" --install "$modern_dir/build/vulkan-loader" > "$reports/vulkan-loader-install.log" 2>&1
"$cmake_bin" -S "$vulkan_dir" -B "$modern_dir/build/macos-vulkan" "${common[@]}" > "$reports/vulkan-configure.log" 2>&1
"$cmake_bin" --build "$modern_dir/build/macos-vulkan" --parallel 4 > "$reports/vulkan-build.log" 2>&1
"$modern_dir/tools/python/bin/ctest" --test-dir "$modern_dir/build/macos-vulkan" --output-on-failure > "$reports/vulkan-ctest.log" 2>&1
app_dir="$vulkan_dir/SWG Vulkan Viewer.app"
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Resources"
cp "$modern_dir/build/macos-vulkan/vulkan-viewer" "$app_dir/Contents/MacOS/SWGVulkanViewer"
cp "$vulkan_dir/Info.plist" "$app_dir/Contents/Info.plist"
cp "$modern_dir/vendor/SWGVulkanClient/LICENSE.md" "$app_dir/Contents/Resources/SWGVulkanClient-LICENSE.md"
codesign --force --sign - "$app_dir"
echo "Built ARM64 Vulkan app: $app_dir"
world_app="$vulkan_dir/SWG Offline World.app"
mkdir -p "$world_app/Contents/MacOS" "$world_app/Contents/Resources"
cp "$modern_dir/build/macos-vulkan/swg-offline-world" "$world_app/Contents/MacOS/SWGOfflineWorld"
cp "$vulkan_dir/World-Info.plist" "$world_app/Contents/Info.plist"
cp "$modern_dir/vendor/SWGVulkanClient/LICENSE.md" "$world_app/Contents/Resources/SWGVulkanClient-LICENSE.md"
codesign --force --sign - "$world_app"
echo "Built ARM64 offline world app: $world_app"
