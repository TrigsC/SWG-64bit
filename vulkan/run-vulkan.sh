#!/bin/bash
set -euo pipefail
vulkan_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$vulkan_dir")"
if [[ ! -f "$modern_dir/viewer/cache/scene.bin" ]]; then
    "$modern_dir/build/viewer/asset-exporter" "$(dirname "$modern_dir")" "$modern_dir/viewer/cache"
fi
app_dir="$vulkan_dir/SWG Vulkan Viewer.app"
if [[ ! -x "$app_dir/Contents/MacOS/SWGVulkanViewer" ]]; then "$vulkan_dir/build-vulkan.sh"; fi
if [[ "${1:-}" == "--smoke" ]]; then
    "$modern_dir/build/macos-vulkan/vulkan-viewer" "$modern_dir/viewer/cache/scene.bin" --smoke "$vulkan_dir/screenshots"
elif [[ $# -eq 0 ]]; then open "$app_dir"
else echo "Usage: $0 [--smoke]" >&2; exit 1
fi
