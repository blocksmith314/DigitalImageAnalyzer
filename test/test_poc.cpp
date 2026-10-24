#include <cstdio>
#include "core/nalunit.h"
#include "core/bitstreamparser.h"
#include "core/h264types.h"

int main() {
    BitstreamParser parser(CodecType::H264);
    if (!parser.parseFile("test/output.h264")) {
        printf("parse FAILED: %s\n", parser.lastError().c_str());
        return 1;
    }
    auto &nals = parser.nalUnits();
    
    H264SPS sps;
    for (auto &nal : nals) {
        if (nal.nalType == 7) { parseH264SPS(nal, sps); break; }
    }
    printf("SPS: poc_type=%d log2_max_poc_lsb=%d log2_max_frame_num=%d\n",
        sps.pic_order_cnt_type, sps.log2_max_pic_order_cnt_lsb_minus4, sps.log2_max_frame_num_minus4);
    
    int shown = 0;
    for (auto &nal : nals) {
        if (nal.nalType >= 1 && nal.nalType <= 5) {
            H264SliceHeader sh;
            bool ok = parseH264SliceHeader(nal, sps, sh);
            printf("  nalType=%d ok=%d first_mb=%u frame_num=%u poc=%u\n",
                nal.nalType, ok, sh.first_mb_in_slice, sh.frame_num, sh.pic_order_cnt_lsb);
            if (++shown >= 15) break;
        }
    }
    return 0;
}
