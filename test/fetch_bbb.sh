#!/usr/bin/env bash
# Downloads Big Buck Bunny (CC-BY 3.0, Blender Foundation) and prepares sample
# bitstreams for Digital Image Analyzer:
#   bbb_720p.mp4   - H.264 MP4 (video only)
#   bbb_720p.h264  - H.264 Annex B raw stream
#   bbb_720p.h265  - H.265/HEVC Annex B raw stream (re-encoded)
#   bbb_av.mp4     - H.264 + AAC tone, for A/V sync testing
set -euo pipefail

DIR="${1:-samples}"
mkdir -p "$DIR"

URL="https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_1MB.mp4"
MP4="$DIR/bbb_720p.mp4"

if [ ! -f "$MP4" ]; then
    echo "downloading Big Buck Bunny (720p, 10s)..."
    curl -fL --retry 3 --max-time 120 -o "$MP4" "$URL"
fi

echo "creating Annex B H.264 stream..."
ffmpeg -y -hide_banner -loglevel error -i "$MP4" \
    -c:v copy -bsf:v h264_mp4toannexb -f h264 "$DIR/bbb_720p.h264"

echo "creating HEVC stream..."
ffmpeg -y -hide_banner -loglevel error -i "$MP4" \
    -c:v libx265 -preset medium -crf 22 -x265-params log-level=error \
    -f hevc "$DIR/bbb_720p.h265"

echo "creating MP4 with AAC audio..."
ffmpeg -y -hide_banner -loglevel error -i "$MP4" \
    -f lavfi -i "sine=frequency=440:duration=10" \
    -map 0:v -map 1:a -c:v copy -c:a aac -b:a 128k "$DIR/bbb_av.mp4"

echo "Big Buck Bunny (c) Blender Foundation | CC-BY 3.0 | https://peach.blender.org"
ls -la "$DIR"
