#!/bin/bash
set -euo pipefail
client_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$client_dir")"
app_dir="$client_dir/SWG Native Client.app"
"$modern_dir/tools/python/bin/cmake" -S "$modern_dir/vulkan" -B "$modern_dir/build/macos-vulkan"
"$modern_dir/tools/python/bin/cmake" --build "$modern_dir/build/macos-vulkan" --target dummyclient --parallel 4
world_dir="$app_dir/Contents/Helpers/SWG Native World.app"
mkdir -p "$world_dir/Contents/MacOS"
mkdir -p "$app_dir/Contents/MacOS" "$app_dir/Contents/Helpers" "$app_dir/Contents/Resources"
swiftc -O -target arm64-apple-macos26.0 -module-cache-path /tmp/swg-native-swift-cache -framework AppKit "$client_dir/src/LoginApp.swift" -o "$app_dir/Contents/MacOS/SWGNativeClient.next"
mv -f "$app_dir/Contents/MacOS/SWGNativeClient.next" "$app_dir/Contents/MacOS/SWGNativeClient"
# Replace executables atomically: a running session retains its old mapped
# inode until reconnect instead of having its binary overwritten in place.
cp "$modern_dir/build/macos-vulkan/swg/tools/dummyclient/dummyclient" "$world_dir/Contents/MacOS/SWGNativeWorld.next"
mv -f "$world_dir/Contents/MacOS/SWGNativeWorld.next" "$world_dir/Contents/MacOS/SWGNativeWorld"
cp "$modern_dir/vendor/SWGVulkanClient/LICENSE.md" "$app_dir/Contents/Resources/SWGVulkanClient-LICENSE.md"
"$modern_dir/tools/python/bin/python" "$client_dir/package-client.py"
codesign --force --sign - "$world_dir"
codesign --force --sign - "$app_dir"
codesign --verify --strict --all-architectures "$app_dir"
