#!/usr/bin/env bash
# CPU code generation and Metal syntax only. Never creates a GPU device or pipeline.
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 BUILD [OUTPUT]" >&2
    exit 1
fi
build=$(cd "$1" && pwd)
source=$(cd "$(dirname "$0")" && pwd)
output=${2:-"$build/replay-metal"}
mkdir -p "$output"
if ! "$build/spirv-cross-msl-capture-replay-test" "$source/msl_capture_layout_simple.spv" "$source/msl_capture_layout_complex.spv" "$source/msl_capture_replay_effects.spv" "$source/msl_capture_replay_unsupported.spv" "$source/msl_capture_layout_unsupported.spv" "$output" > "$output/api.log" 2>&1; then
    cat "$output/api.log" >&2
    exit 1
fi
count=0
: > "$output/syntax.log"
for shader in "$output"/*.metal; do
    if ! xcrun metal -x metal -std=macos-metal2.4 -fsyntax-only -fno-modules "$shader" >> "$output/syntax.log" 2>&1; then
        cat "$output/syntax.log" >&2
        exit 1
    fi
    count=$((count + 1))
done
printf 'API checks and %s Metal 2.4 syntax checks passed (CPU only).\n' "$count"
