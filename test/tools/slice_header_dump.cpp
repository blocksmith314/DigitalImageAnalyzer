#include <cstdio>

#include "bitstreamparser.h"
#include "h264types.h"
#include "nalunit.h"

// Prints the SPS POC configuration and the first slice headers of an H.264
// Annex B stream. Pass the stream path as the first argument.
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

    auto &nals = parser.nalUnits();

    H264SPS sps;
    bool haveSps = false;
    for (auto &nal : nals) {
        if (nal.nalType == 7) {
            haveSps = parseH264SPS(nal, parser.nalData(nal), sps);
            break;
        }
    }
    if (!haveSps) {
        std::printf("no SPS found\n");
        return 1;
    }

    std::printf("SPS: poc_type=%u log2_max_poc_lsb=%u log2_max_frame_num=%u\n",
                sps.pic_order_cnt_type, sps.log2_max_pic_order_cnt_lsb_minus4,
                sps.log2_max_frame_num_minus4);

    int shown = 0;
    for (auto &nal : nals) {
        if (nal.nalType < 1 || nal.nalType > 5) continue;
        H264SliceHeader header;
        const bool ok = parseH264SliceHeader(nal, parser.nalData(nal), sps, header);
        std::printf("  nalType=%u ok=%d first_mb=%u frame_num=%u poc=%u\n",
                    nal.nalType, ok, header.first_mb_in_slice, header.frame_num,
                    header.pic_order_cnt_lsb);
        if (++shown >= 15) break;
    }
    return 0;
}
