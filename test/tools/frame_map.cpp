#include <cstdio>

#include "bitstreamparser.h"
#include "nalanalyzer.h"
#include "nalunit.h"

// Prints how NAL units are grouped into frames (PTS order) by NALAnalyzer.
// Pass the stream path as the first argument.
int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <h264-file>\n", argv[0]);
        return 2;
    }

    BitstreamParser parser(CodecType::H264);
    if (!parser.parseFile(argv[1])) {
        std::printf("parse FAILED: %s\n", parser.lastError().c_str());
        return 1;
    }

    auto &nals = parser.nalUnitsMut();
    NALAnalyzer analyzer;
    analyzer.analyzeAll(parser);

    std::printf("Stats: frameCount=%u codec=%d\n", analyzer.stats().frameCount,
                static_cast<int>(analyzer.stats().codec));

    std::printf("\n=== First 20 slice NALs ===\n");
    int shown = 0;
    for (auto &nal : nals) {
        if (nal.nalType < 1 || nal.nalType > 5) continue;
        std::printf("  offset=%llu frameIndex=%d frameType=%c nalType=%u\n",
                    static_cast<unsigned long long>(nal.offset), nal.frameIndex,
                    nal.frameType ? nal.frameType : '?', nal.nalType);
        if (++shown >= 20) break;
    }

    std::printf("\n=== Grouped by frameIndex ===\n");
    int current = -1;
    for (auto &nal : nals) {
        if (nal.nalType < 1 || nal.nalType > 5) continue;
        if (nal.frameIndex != current) {
            current = nal.frameIndex;
            std::printf("Frame idx=%d type=%c nalType=%u\n", current,
                        nal.frameType ? nal.frameType : '?', nal.nalType);
        }
    }
    return 0;
}
