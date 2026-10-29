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
| `test/tools/tidy.sh` | run clang-tidy on changed files (or `--all`) |

Build outputs go to `build/test/tools/`. These tools are grouped in the `tools`
IDE folder and are not registered with CTest.

## Layout

```
main.cpp, mainwindow.*, nallistmodel.*   Qt UI
core/                                    parsing only (no Qt, no FFmpeg)
ffmpeg/                                  demux, decode, audio
test/                                    gtest sources and fixtures
test/tools/                              developer tools
scripts/                                 packaging helpers (macOS and Windows)
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

Follow the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html),
C++17. Formatting is defined by `.clang-format`; run `clang-format` on the files
you change and keep reformat-only changes in a separate PR.

Static analysis is defined by `.clang-tidy` and run through
`test/tools/tidy.sh` (changed files) or `test/tools/tidy.sh --all`. It needs
`build/compile_commands.json`, which CMake exports automatically. Do not enable
`CMAKE_CXX_CLANG_TIDY` globally: it makes every build much slower.

**Naming**

Types and functions use mixed case (`MainWindow`, `LoadFile`); variables and
parameters use `snake_case`; members use `snake_case_` with a trailing
underscore; constants and enum values use `kPascalCase`. Prefer `enum class`,
lowercase file names, and `//` comments in English that explain *why*. Keep
related types together in one header. Qt virtual overrides keep their Qt names
(`rowCount`, `columnCount`, event handlers).

**Headers and dependencies**

- Keep FFmpeg includes in `.cpp` files; public headers must not expose FFmpeg
  or Qt Multimedia types.
- Includes are sorted by `.clang-format` (the file's own header stays first);
  keep new includes in the matching block and put FFmpeg `extern "C"` last.
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

- Follow the existing `bool` return + `LastError()` pattern.
- Do not throw exceptions across module or FFmpeg boundaries; convert failures
  to `false` + message.
- User-facing failures are reported once at the call site, not logged in loops.

**Performance**

- Avoid heap allocation in hot paths (per-NAL parsing, per-frame and
  per-paint model code); cache constant strings and colors.
- Never store NAL payloads in `NALUnit`; keep offsets/sizes and access bytes
  through the parser.
- Keep the GUI thread responsive: use incremental progress or background work
  for long operations.

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
  `NALUnit` (use `BitstreamParser::NalData()`).
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
