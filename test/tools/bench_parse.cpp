// Multi-mode parser benchmark. Not part of the automated test suite.
//
//   bench_parse <mode> <file> [--repeat N]
//
// Modes: raw-h264, raw-h265, buffer, mp4-index, mp4-avcc

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "bitstreamparser.h"
#include "nalanalyzer.h"

namespace {

using Clock = std::chrono::steady_clock;

double to_ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

struct Run {
  double parse_ms = 0;
  double analyze_ms = 0;
  double total_ms = 0;
  size_t nals = 0;
  uint32_t frames = 0;
};

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return false;
  const std::streamsize size = file.tellg();
  if (size <= 0) return false;
  out.resize(static_cast<size_t>(size));
  file.seekg(0);
  return static_cast<bool>(
      file.read(reinterpret_cast<char*>(out.data()), size));
}

bool run_once(const std::string& mode, const std::string& path,
              const std::vector<uint8_t>& buffer, Run& run,
              std::string& error) {
  std::unique_ptr<BitstreamParser> parser;
  CodecType codec = CodecType::kH264;

  if (mode == "raw-h264") {
    parser = std::make_unique<BitstreamParser>(CodecType::kH264);
  } else if (mode == "raw-h265") {
    parser = std::make_unique<BitstreamParser>(CodecType::kH265);
  } else if (mode == "buffer") {
    parser = std::make_unique<BitstreamParser>(CodecType::kH264);
  } else if (mode == "mp4-index" || mode == "mp4-avcc") {
    parser = std::make_unique<BitstreamParser>(CodecType::kH264);
  } else {
    error = "unknown mode: " + mode;
    return false;
  }

  const auto t0 = Clock::now();
  bool parsed = false;
  if (mode == "raw-h264" || mode == "raw-h265") {
    parsed = parser->ParseFile(path);
  } else if (mode == "buffer") {
    parsed = parser->ParseBuffer(buffer.data(), buffer.size());
  } else if (mode == "mp4-index") {
    parsed = parser->ParseMp4Indexed(path, codec);
  } else {
    parsed = parser->ParseAvcc(path, codec);
  }
  const auto t1 = Clock::now();
  if (!parsed) {
    error = parser->LastError();
    return false;
  }

  NALAnalyzer analyzer;
  if (!analyzer.AnalyzeAll(*parser)) {
    error = "AnalyzeAll failed";
    return false;
  }
  const auto t2 = Clock::now();

  run.parse_ms = to_ms(t1 - t0);
  run.analyze_ms = to_ms(t2 - t1);
  run.total_ms = to_ms(t2 - t0);
  run.nals = parser->NalUnits().size();
  run.frames = analyzer.stats().FrameCount;
  return true;
}

void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s <mode> <file> [--repeat N]\n"
               "modes: raw-h264 raw-h265 buffer mp4-index mp4-avcc\n",
               argv0);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    usage(argv[0]);
    return 2;
  }

  const std::string mode = argv[1];
  const std::string path = argv[2];
  int repeat = 5;
  for (int i = 3; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--repeat") == 0) repeat = std::atoi(argv[++i]);
  }
  if (repeat <= 0) repeat = 1;

  std::vector<uint8_t> buffer;
  if (mode == "buffer" && !read_file(path, buffer)) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    return 1;
  }
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);

  std::vector<Run> runs;
  for (int i = 0; i < repeat; i++) {
    Run run;
    std::string error;
    if (!run_once(mode, path, buffer, run, error)) {
      std::fprintf(stderr, "run %d failed: %s\n", i + 1, error.c_str());
      return 1;
    }
    runs.push_back(run);
    std::printf("run %d: parse=%.1f ms analyze=%.1f ms total=%.1f ms\n", i + 1,
                run.parse_ms, run.analyze_ms, run.total_ms);
  }

  auto median_of = [](std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  };
  std::vector<double> parse_times, analyzeTimes, totalTimes;
  for (const Run& run : runs) {
    parse_times.push_back(run.parse_ms);
    analyzeTimes.push_back(run.analyze_ms);
    totalTimes.push_back(run.total_ms);
  }

  const Run& last = runs.back();
  const double parse_ms = median_of(parse_times);
  const double analyze_ms = median_of(analyzeTimes);
  const double total_ms = median_of(totalTimes);
  const double mb =
      ec ? 0.0 : static_cast<double>(file_size) / (1024.0 * 1024.0);
  const double mb_per_sec = total_ms > 0 ? mb / (total_ms / 1000.0) : 0.0;

  std::printf("\nmode=%s size=%.1f MB nals=%zu frames=%u\n", mode.c_str(), mb,
              last.nals, last.frames);
  std::printf("median: parse=%.1f ms analyze=%.1f ms total=%.1f ms\n", parse_ms,
              analyze_ms, total_ms);
  std::printf("throughput: %.0f MB/s, %.2f us/NAL\n", mb_per_sec,
              last.nals ? total_ms * 1000.0 / last.nals : 0.0);
  return 0;
}
