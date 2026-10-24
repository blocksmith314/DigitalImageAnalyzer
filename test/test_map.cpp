#include <cstdio>
#include "core/nalunit.h"
#include "core/bitstreamparser.h"
#include "core/nalanalyzer.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/Users/odyssey/ClionProjects/DigitalImageAnalyzer/test/output.h264";
    printf("=== Test NALAnalyzer POC sorting ===\n");
    
    BitstreamParser parser(CodecType::H264);
    if (!parser.parseFile(path)) {
        printf("parse FAILED: %s\n", parser.lastError().c_str());
        return 1;
    }
    
    auto &nals = parser.nalUnitsMut();
    NALAnalyzer analyzer;
    analyzer.analyzeAll(nals);
    
    printf("Stats: frameCount=%u codec=%d\n", analyzer.stats().frameCount, (int)analyzer.stats().codec);
    
    // Print first frame and last few frames
    printf("\n=== First 20 slice NALs ===\n");
    int shown = 0;
    for (auto &nal : nals) {
        bool isSlice = nal.nalType >= 1 && nal.nalType <= 5;
        if (isSlice) {
            printf("  offset=%llu frameIndex=%d frameType=%c nalType=%d\n",
                (unsigned long long)nal.offset, nal.frameIndex, nal.frameType, nal.nalType);
            if (++shown >= 20) break;
        }
    }
    
    printf("\n=== Grouped by frameIndex ===\n");
    int cur = -1;
    for (auto &nal : nals) {
        bool isSlice = nal.nalType >= 1 && nal.nalType <= 5;
        if (isSlice && nal.frameIndex != cur) {
            cur = nal.frameIndex;
            printf("Frame idx=%d type=%c nalType=%d\n", cur, nal.frameType, nal.nalType);
        }
    }
    return 0;
}