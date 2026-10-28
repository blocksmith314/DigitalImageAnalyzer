# Digital Image Analyzer

**H.264/AVC 与 H.265/HEVC 码流分析与播放工具。**

[English](README.md) | **简体中文**

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
[![Platforms](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey)](#构建)
[![Latest release](https://img.shields.io/github/v/release/blocksmith314/DigitalImageAnalyzer)](https://github.com/blocksmith314/DigitalImageAnalyzer/releases)

Digital Image Analyzer 是一款桌面工具，面向视频工程师、学生以及任何想了解压缩视频内部结构的人。它支持裸 Annex B 码流与 MP4/MOV/MKV/TS 容器，解析每个 NAL 单元、参数集、切片头和 SEI 消息，并将结构视图与逐帧播放结合起来。

NAL 列表与播放器双向联动：拖动时间轴时，当前帧对应的 NAL 行会高亮；点击某个 NAL，播放器会跳转到包含它的帧。

## 截图

![Digital Image Analyzer 主界面](images/image_analyzer.png)

## 核心亮点

- **解码顺序 vs 显示顺序** —— 每个访问单元同时显示 DTS 和 PTS，B 帧重排序一目了然。
- **完整参数集解析** —— 逐字段解析 SPS/PPS（HEVC 还包含 VPS）：profile、level、色度格式、位深、裁剪、VUI 时序、POC 类型、slice group、8x8 变换等。
- **SEI 消息** —— 用户数据（编码器名称与参数）、恢复点、主显示色彩体积、内容光级、替代传输特性。
- **面向大文件** —— 内存映射解析、MP4 采样表快速索引、惰性解码，数 GB 文件依然流畅。
- **帧精确播放** —— 播放/暂停、倍速、可配置单步、全屏，以及 PTS 驱动的音画同步。

## 支持的输入

| 类型 | 格式 |
|------|------|
| 裸码流 | `.h264`、`.264`、`.h265`、`.265`、`.hevc`（Annex B） |
| 容器 | MP4、MOV、M4V、MKV、TS |
| 视频编码 | H.264/AVC、H.265/HEVC |
| 音频播放 | AAC、MP3、AC-3 及其它 FFmpeg 解码器 |

## 功能

### 码流分析

- NAL 单元列表：类型、名称、大小、文件偏移、帧类型、DTS、PTS
- 切片头详情：first MB、slice 类型、POC、参考索引
- 码流统计：分辨率、帧率、帧数、I/P/B 分布、码率、GOP 大小、NAL 大小分布
- SPS/PPS/VPS 字段、SEI 载荷和切片信息的详细视图
- 帧类型配色，以及表格与播放器之间的联动高亮

### 播放

- 播放/暂停、停止、上一帧/下一帧，单步 1–1000 可配置
- 0.25x–3x 倍速播放
- 双击或工具栏按钮进入全屏（`Esc` 退出）
- 环形缓冲音频输出，PTS 驱动音画同步

### 大文件

- 内存映射 I/O：只解析 NAL 元数据，不加载整个文件
- MP4 采样表快速路径，按需解码帧
- 加载进度可取消，状态栏显示各阶段耗时

## 环境要求

- CMake 3.21+，支持 C++17 的编译器
- Qt 6.4+（**Widgets** 和 **Multimedia** 模块）
- FFmpeg 5+（`libavformat`、`libavcodec`、`libavutil`、`libswscale`、`libswresample`）
- `pkg-config`

## 构建

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

### Windows（MSYS2，MINGW64 终端）

```sh
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-pkgconf mingw-w64-x86_64-qt6-base \
          mingw-w64-x86_64-qt6-multimedia mingw-w64-x86_64-ffmpeg
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/mingw64
cmake --build build
```

### 辅助工具

构建还会在 `build/test/` 下生成几个小工具：

```sh
# 生成合成裸流（宽 高 帧率 帧数 keyint 编码器 输出文件）
./build/test/tools/gen 1280 720 30 120 30 libx264 /tmp/test.h264

# 打印随附样例的 H.264 切片头（frame_num / POC）
./build/test/tools/slice_header_dump

# 校验 NAL 分组与 PTS 顺序
./build/test/tools/frame_map test/data/output.h264
```

## 使用

| 操作 | 快捷键 / 控件 |
|------|---------------|
| 打开文件 | `Ctrl+O`（文件 → 打开） |
| 播放 / 暂停 | `Space` |
| 上一帧 / 下一帧 | `←` / `→`（步长由 **Step** 决定） |
| 全屏 | 双击视频或点击全屏按钮；`Esc` 退出 |
| 跳转 | 拖动时间轴滑块 |
| 倍速 | 工具栏下拉框 |
| 查看 NAL 详情 | 点击 NAL 表中的对应行 |
| 跳转到帧 | 拖动时间轴，匹配的 NAL 行会高亮 |

**Step** 数值框决定方向键与上一帧/下一帧按钮的步长（1–1000）。

## 发布产物

预编译版本见
[Releases 页面](https://github.com/blocksmith314/DigitalImageAnalyzer/releases)：

- macOS（Apple Silicon 与 Intel）
- Linux（AppImage）
- Windows（x64）

## 第三方组件

本项目在运行时会打包 Qt 与 FFmpeg。具体许可证与源码地址见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 许可证

本项目采用 **GNU General Public License v3.0 or later**（GPL-3.0-or-later）发布。
详见 [LICENSE](LICENSE)。

## 贡献

欢迎在 <https://github.com/blocksmith314/DigitalImageAnalyzer> 提交 Issue 或 PR。
反馈具体文件的问题时，请附上编码格式、容器格式，以及分析器显示的 profile/level
和帧数，这些信息会很有帮助。
