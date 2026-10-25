# Third-Party Notices

Digital Image Analyzer is distributed under the GNU General Public License
v3.0 or later (see `LICENSE`). The release binaries bundle third-party
components whose licenses are reproduced or referenced below.

## Qt 6

- **License:** LGPL-3.0 (or GPL-3.0, depending on the build)
- **Project:** <https://www.qt.io/>
- **Source:** <https://code.qt.io/cgit/qt/>
- **Bundled modules:** Qt Core, Gui, Widgets, Multimedia, Network and their
  plugins; on macOS also QtQml/QtQuick when pulled in by Qt Multimedia.

Qt is linked dynamically. The shared libraries are bundled unmodified and can
be replaced by the user (on macOS inside `Contents/Frameworks`, on Windows the
Qt DLLs next to the executable, on Linux inside the AppImage). For LGPL
compliance the corresponding source code is available from the URLs above and
upon request.

## FFmpeg

- **License:** GPL-2.0-or-later (the builds used by the release artifacts are
  configured with GPL components such as libx264/libx265; FFmpeg itself is also
  available under LGPL-2.1-or-later when built without `--enable-gpl`)
- **Project:** <https://ffmpeg.org/>
- **Source:** <https://ffmpeg.org/download.html>
- **Bundled libraries:** libavformat, libavcodec, libavutil, libswscale,
  libswresample plus their codec dependencies (x264, x265, dav1d, SVT-AV1,
  libvpx, OpenSSL, zlib, ...).

## ICU (International Components for Unicode)

- **License:** Unicode License v3
- **Project:** <https://icu.unicode.org/>
- Bundled on macOS/Linux as a dependency of the Qt build in use.

## Other dependencies

The following libraries may be linked by the FFmpeg/Qt builds used for the
release artifacts and are distributed under their respective licenses:

| Component | License |
|-----------|---------|
| OpenSSL | Apache-2.0 |
| libx264 / libx265 | GPL-2.0-or-later |
| dav1d | BSD-2-Clause |
| SVT-AV1 | BSD-2-Clause-Patent |
| libvpx | BSD-3-Clause |
| zlib | Zlib |
| Brotli | MIT |

If a required notice is missing, please open an issue and it will be added.
