#!/bin/bash
set -euo pipefail
world_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$world_dir")"
app_dir="$world_dir/SWG Offline World.app"
if [[ ! -x "$app_dir/Contents/MacOS/SWGOfflineWorld" ]]; then "$world_dir/build-vulkan.sh"; fi
if [[ $# -eq 0 ]]; then
    open "$app_dir"
elif [[ $# -eq 1 && "$1" == "--smoke" ]]; then
    exec "$modern_dir/build/macos-vulkan/swg-offline-world" --smoke "$world_dir/world-screenshots"
elif [[ $# -eq 1 && "$1" == "--action-smoke" ]]; then
    exec "$modern_dir/build/macos-vulkan/swg-offline-world" --action-smoke "$world_dir/action-screenshots"
else
    echo "Usage: $0 [--smoke | --action-smoke]" >&2
    exit 2
fi
