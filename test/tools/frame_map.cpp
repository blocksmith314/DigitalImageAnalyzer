#include <cstdio>

#include "bitstreamparser.h"
#include "nalanalyzer.h"
#include "nalunit.h"

// Prints how NAL units are grouped into frames (PTS order) by NALAnalyzer.
// Pass the stream path as the first argument.
int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <h264-file>\n", argv[0]);
    return 2;
  }

  BitstreamParser parser(CodecType::kH264);
  if (!parser.ParseFile(argv[1])) {
    std::printf("parse FAILED: %s\n", parser.LastError().c_str());
    return 1;
  }

  auto& nals = parser.NalUnitsMut();
  NALAnalyzer analyzer;
  analyzer.AnalyzeAll(parser);

  std::printf("Stats: FrameCount=%u codec=%d\n", analyzer.stats().FrameCount,
              static_cast<int>(analyzer.stats().codec));

  std::printf("\n=== First 20 slice NALs ===\n");
  int shown = 0;
  for (auto& nal : nals) {
    if (nal.nal_type < 1 || nal.nal_type > 5) continue;
    std::printf("  offset=%llu frame_index=%d frame_type=%c nal_type=%u\n",
                static_cast<unsigned long long>(nal.offset), nal.frame_index,
                nal.frame_type ? nal.frame_type : '?', nal.nal_type);
    if (++shown >= 20) break;
  }

  std::printf("\n=== Grouped by frame_index ===\n");
  int current = -1;
  for (auto& nal : nals) {
    if (nal.nal_type < 1 || nal.nal_type > 5) continue;
    if (nal.frame_index != current) {
      current = nal.frame_index;
      std::printf("Frame idx=%d type=%c nal_type=%u\n", current,
                  nal.frame_type ? nal.frame_type : '?', nal.nal_type);
    }
  }
  return 0;
}
