# AGENTS.md

Repository instructions for AI coding agents (and humans) working on this
project. User-facing documentation lives in `README.md` / `README_CN.md`.

## Project

Digital Image Analyzer: a Qt 6 + FFmpeg desktop application that analyzes and
plays H.264/AVC and H.265/HEVC bitstreams. C++17, CMake + Ninja.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Dependencies (macOS): `brew install cmake ninja qt ffmpeg pkg-config`.
See `README.md` for Debian/Ubuntu and MSYS2 commands.

Options:

- `-DBUILD_TOOLS=ON|OFF` (default ON): developer tools under `test/tools/`.
- `-DBUILD_TEST=ON|OFF` (default ON): GoogleTest suite; use `OFF` for release
  builds (the release workflow already does).

## Test

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

- Suites: core parsing, MP4 index, FFmpeg probe/decoder/audio, NalListModel,
  MainWindow interaction. Run `ctest -N` for the current list; a much shorter
  list (or `*_NOT_BUILT` entries) means the build is stale.
- Fixtures live in `test/data/` and are located through the `TEST_DATA_DIR`
  compile definition; tests skip when a fixture is missing.
- GUI tests run with `QT_QPA_PLATFORM=offscreen` (set via CTest properties).
- gtest discovery is POST_BUILD: build before running `ctest`, otherwise stale
  `*_NOT_BUILT` entries can appear.
- New parser paths should get a fixture plus tests; keep `test/data` small.

## Tools (not tests)

| Tool | Purpose |
|------|---------|
| `test/tools/gen` | generate synthetic raw H.264/H.265 streams |
| `test/tools/slice_header_dump` | print SPS POC config and slice headers |
| `test/tools/frame_map` | print NAL-to-frame grouping |
| `test/tools/bench_parse` | multi-mode benchmark (`raw-h264`, `raw-h265`, `buffer`, `mp4-index`, `mp4-avcc`) |
| `test/tools/profile_parse.sh` | benchmark + macOS `sample` hot-function report |

Build outputs go to `build/test/tools/`. Both tools are grouped in the `tools`
IDE folder and are not registered with CTest.

## Layout

```
main.cpp, mainwindow.*, nallistmodel.*   Qt UI
core/                                    parsing only (no Qt, no FFmpeg)
ffmpeg/                                  demux, decode, audio
test/                                    gtest sources and fixtures
test/tools/                              developer tools
scripts/                                 packaging helpers (macOS)
```

Keep `core/` free of Qt/FFmpeg dependencies: it is what the parser tests link.

## Conventions

- Comments and identifiers in English; user-visible strings may be English.
- Conventional commits: `feat:`, `fix:`, `test:`, `docs:`, `build:`, `chore:`.
  Reference issues with `Fixes #N` in the commit/PR body.
- **Do not commit directly to `main`.** Create a branch and open a PR; merge
  after review/tests. (Only this file and trivial reverts are exceptions in
  practice, still prefer a PR.)
- One top-level `CMakeLists.txt`; do not add another project-level one.
- Prefer `std::string_view`/`const char*` for constant names; avoid per-frame
  allocations in model paint code.
- `third_party/googletest` is git-ignored and fetched on demand.

## Coding conventions

Baseline: the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
C++17. Where the rules below are silent, follow Google style and match the file
you are editing.

Formatting is defined by `.clang-format` at the repository root, which must stay
in sync with the existing style (4 spaces, same-line braces, ~100 columns,
`Type &name`). Run `clang-format` on the files you change; reformat-only changes
belong in a separate PR. The deviations in the table are deliberate: they describe the
existing tree and changing them would be a whole-repository rename/reformat.

**Deviations from Google style**

| Topic | Google style | This project |
|-------|--------------|--------------|
| Indentation | 2 spaces | 4 spaces |
| Function/variable names | `snake_case` | `camelCase` |
| Member variables | trailing underscore (`member_`) | `m_` prefix |
| Line length | 80 columns | ~100 columns |
| Header guard | `#ifndef` guard | `#pragma once` |
| Exceptions | not used | not thrown across module/FFmpeg boundaries |

Everything else follows Google: `PascalCase` types, `kPascalCase` constants,
`enum class`, lowercase file names, one class per header pair, and
`//` comments in English that explain *why*.

**Headers and dependencies**

- Keep FFmpeg includes in `.cpp` files; public headers must not expose FFmpeg
  or Qt Multimedia types.
- Include order: own header, Qt, standard library, `extern "C"` FFmpeg.
- No new third-party dependency without discussion; `third_party/` is
  git-ignored and fetched on demand.
- OS-specific code belongs in `core/platformfile.*`; do not spread `#ifdef
  _WIN32` through the code base.
- Prefer forward declarations over includes where possible (Google's
  "Include What You Use" still applies to what the header actually needs).

**Ownership and resources**

- Own heap objects with `std::unique_ptr`/`std::vector`; use move-only types
  (see `MappedFile`, `BitstreamParser`).
- Raw pointers are only for non-owning references or FFmpeg/Qt objects that
  the library frees (`av_*_free`, Qt parent ownership).
- Use RAII wrappers for FFmpeg contexts; check every FFmpeg return code.
- No `new`/`delete` outside ownership wrappers; no `malloc` in C++ code.

**Errors**

- Follow the existing `bool` return + `lastError()` pattern.
- Do not throw exceptions across module or FFmpeg boundaries; convert failures
  to `false` + message.
- User-facing failures are reported once at the call site, not logged in loops.

**Performance**

- No heap allocation in per-NAL or per-frame/per-paint paths (see
  `NalListModel::data()`); cache constant strings/colors instead.
- Never copy NAL payloads into `NALUnit`; use `BitstreamParser::nalData()`.
- Use `std::string_view`/`const char*` for constant names, not `std::string`.
- Long operations must not block the GUI thread for their whole duration;
  prefer incremental progress or background work when adding new ones.

**Qt**

- Use `QStringLiteral` for literals and `QString::fromLatin1(view.data(), n)`
  for ASCII names; avoid `QString::fromStdString()` in hot paths.
- Respect QObject ownership: pass a parent instead of manual `delete`.
- Do not touch widgets from non-GUI threads; use signals/queued connections.
- Prefer Qt containers only at the Qt boundary; `std::` containers inside
  `core/` and `ffmpeg/`.

**Tests**

- New behaviour needs a test; parser paths need a fixture in `test/data/`.
- Keep fixtures small; tests must skip cleanly when a fixture is absent.

## Requirements (keep true; gaps are tracked in issues)

- `core/` must not depend on Qt or FFmpeg.
- Large files: parse with mmap and metadata only; never store NAL payloads in
  `NALUnit` (use `BitstreamParser::nalData()`).
- Path handling must be UTF-8-safe; on Windows use the wide-character file APIs.
- Codec detection must not rely on a single NAL header; score several headers.
- Decoding and audio must stay testable headless (`AudioDecoder` has no Qt
  Multimedia dependency).
- Release builds use `-DBUILD_TEST=OFF`; the macOS bundle script reports the
  minimum required macOS version and CI enforces it.

Open bugs and TODOs are tracked in GitHub issues, not in this file.

## Performance work

- Measure with `test/tools/bench_parse` (median of several runs) before and
  after a change.
- Profile with `test/tools/profile_parse.sh` (macOS `sample`).
- Avoid per-NAL and per-frame allocations in parsing and model paint paths.
- Do not micro-optimise without a benchmark; large-input parsing is I/O-bound.
