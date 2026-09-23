#!/usr/bin/env bash
# Renders the demo with the real plugin and encodes docs/demo.mp3 and docs/demo.mp4.
# Needs a build with tests enabled (target ltb_render_demo) and ffmpeg.
#
#   tools/make_demo.sh [build-dir] [seconds] [preset]
set -euo pipefail
build="${1:-build}"
seconds="${2:-40}"
preset="${3:-Night Drift}"
out="$(mktemp -d)"

renderer="$(find "$build" -type f -name ltb_render_demo -perm -u+x | head -n1)"
"$renderer" "$out" "$seconds" "$preset"

mkdir -p docs
# Loudness-normalise to a comfortable streaming level.
ffmpeg -y -loglevel error -i "$out/demo.wav" -af loudnorm=I=-18:TP=-1.5:LRA=11 -ar 48000 \
    -c:a libmp3lame -b:a 160k docs/demo.mp3
ffmpeg -y -loglevel error -framerate 15 -i "$out/frame_%05d.png" -i "$out/demo.wav" \
    -af loudnorm=I=-18:TP=-1.5:LRA=11 -ar 48000 \
    -c:v libx264 -preset slow -crf 30 -pix_fmt yuv420p -vf "scale=1180:760" \
    -c:a aac -b:a 160k -shortest -movflags +faststart docs/demo.mp4
rm -rf "$out"
ls -la docs/demo.mp3 docs/demo.mp4
