#!/bin/bash
set -euo pipefail
viewer_dir="$(cd "$(dirname "$0")" && pwd)"
modern_dir="$(dirname "$viewer_dir")"
client_dir="$(dirname "$modern_dir")"
app_dir="$viewer_dir/SWG Asset Viewer.app"
if [[ ! -x "$app_dir/Contents/MacOS/SWGAssetViewer" || ! -x "$modern_dir/build/viewer/asset-exporter" ]]; then
    "$viewer_dir/build-viewer.sh"
fi
if [[ "${1:-}" == "--refresh" || ! -f "$viewer_dir/cache/scene.bin" ]]; then
    "$modern_dir/build/viewer/asset-exporter" "$client_dir" "$viewer_dir/cache" \
        > "$modern_dir/reports/viewer-export.log" 2>&1
    cat "$modern_dir/reports/viewer-export.log"
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--refresh]" >&2; exit 1
fi
open "$app_dir"
