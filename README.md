# Digital Image Analyzer

A cross-platform desktop application for inspecting and playing **H.264/AVC** and
**H.265/HEVC** video bitstreams. It parses NAL units, SPS/PPS/VPS, slice headers
and SEI messages, shows decode/display order side by side, and plays the video
(with audio) frame by frame.

![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)

## Screenshot

![Digital Image Analyzer main window](images/image_analyzer.png)

## Features

- Opens raw Annex B streams (`.h264`, `.h265`, `.264`, `.265`, `.hevc`) and
  containers (MP4, MOV, MKV, TS)
- Fast import for multi-GB files: memory-mapped parsing, MP4 sample-table
  index, and lazy frame decoding
- NAL unit table with type, name, size, file offset, frame type, DTS and PTS;
  click a row to inspect it, or scrub the timeline to highlight the frame
- Complete H.264/H.265 parameter-set parsing: profile/level, chroma format,
  bit depth, cropping, VUI timing, POC configuration, slice groups,
  8x8 transform and more
- SEI decoding: user data (encoder version/options), recovery point,
  mastering display colour volume, content light level and alternative
  transfer characteristics
- Stream statistics: codec, profile/level, resolution, frame rate, frame count,
  I/P/B distribution, bitrate, GOP structure, NAL size distribution
- Playback: play/pause, playback speed, frame stepping with configurable step,
  fullscreen, and audio output (AAC, MP3, ...) with PTS-driven A/V sync
- Readable status bar with per-stage load timings and load progress with cancel

## Requirements

- CMake 3.21+ and a C++17 compiler
- Qt 6.4+ with the **Widgets** and **Multimedia** modules
- FFmpeg 5+ (`libavformat`, `libavcodec`, `libavutil`, `libswscale`,
  `libswresample`)
- `pkg-config`

## Building

### macOS

```sh
brew install cmake ninja qt ffmpeg pkg-config
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build
```

### Debian / Ubuntu

```sh
sudo apt install cmake ninja-build pkg-config \
    qt6-base-dev qt6-multimedia-dev \
    libavformat-dev libavcodec-dev libavutil-dev \
    libswscale-dev libswresample-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows (MSYS2, MINGW64 shell)

```sh
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-pkgconf mingw-w64-x86_64-qt6-base \
          mingw-w64-x86_64-qt6-multimedia mingw-w64-x86_64-ffmpeg
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
```

## Usage

| Action | Shortcut / control |
|--------|--------------------|
| Open a file | `Ctrl+O` (File → Open) |
| Play / pause | `Space` |
| Previous / next frame | `←` / `→` (see **Step:** in the toolbar) |
| Fullscreen | double-click the video, the fullscreen button, or `Esc` to exit |
| Seek | drag the timeline slider |
| Playback speed | toolbar combo box |

The **Step:** spin box sets how many frames the arrow keys and the
previous/next buttons jump (1–1000).

## Release artifacts

Prebuilt binaries are attached to GitHub Releases:

- macOS (Apple Silicon and Intel)
- Linux (AppImage)
- Windows (x64)

## Third-party components

This project bundles Qt and FFmpeg at runtime. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the exact licenses and
source locations.

## License

Distributed under the **GNU General Public License v3.0 or later**
(GPL-3.0-or-later). See [LICENSE](LICENSE).

## Contributing

Issues and pull requests are welcome at
<https://github.com/blocksmith314/DigitalImageAnalyzer>.
