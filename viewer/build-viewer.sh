#!/bin/bash
set -euo pipefail
viewer_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$viewer_dir")"
cmake_bin="$modern_dir/tools/python/bin/cmake"
ninja_bin="$modern_dir/tools/python/bin/ninja"
build_dir="$modern_dir/build/viewer"
asio_dir="$modern_dir/build/macos-arm64/_deps/asio-src"
mkdir -p "$build_dir" "$modern_dir/build/swift-module-cache" "$modern_dir/reports"
configure_args=(-S "$viewer_dir" -B "$build_dir" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=$ninja_bin" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release)
if [[ -d "$asio_dir/asio" ]]; then configure_args+=("-DFETCHCONTENT_SOURCE_DIR_ASIO=$asio_dir"); fi
"$cmake_bin" "${configure_args[@]}" > "$modern_dir/reports/viewer-configure.log" 2>&1
"$cmake_bin" --build "$build_dir" --target asset-exporter --parallel 4 > "$modern_dir/reports/viewer-build.log" 2>&1
"$build_dir/asset-exporter" --self-test
xcrun swiftc -O -target arm64-apple-macosx14.0 \
    -module-cache-path "$modern_dir/build/swift-module-cache" \
    -framework AppKit -framework Metal -framework MetalKit \
    "$viewer_dir/src/Viewer.swift" -o "$build_dir/SWGAssetViewer" \
    > "$modern_dir/reports/viewer-swift-build.log" 2>&1
app_dir="$viewer_dir/SWG Asset Viewer.app"
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Resources"
cp "$build_dir/SWGAssetViewer" "$app_dir/Contents/MacOS/SWGAssetViewer"
cp "$modern_dir/vendor/SWGVulkanClient/LICENSE.md" "$app_dir/Contents/Resources/SWGVulkanClient-LICENSE.md"
cp "$viewer_dir/Info.plist" "$app_dir/Contents/Info.plist"
codesign --force --sign - "$app_dir"
echo "Built native ARM64 app: $app_dir"
