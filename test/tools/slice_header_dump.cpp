#include <cstdio>

#include "bitstreamparser.h"
#include "h264types.h"
#include "nalunit.h"

// Prints the SPS POC configuration and the first slice headers of an H.264
// Annex B stream. Pass the stream path as the first argument.
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

  auto& nals = parser.NalUnits();

  H264SPS sps;
  bool have_sps = false;
  for (auto& nal : nals) {
    if (nal.nal_type == 7) {
      have_sps = ParseH264Sps(nal, parser.NalData(nal), sps);
      break;
    }
  }
  if (!have_sps) {
    std::printf("no SPS found\n");
    return 1;
  }

  std::printf("SPS: poc_type=%u log2_max_poc_lsb=%u log2_max_frame_num=%u\n",
              sps.pic_order_cnt_type, sps.log2_max_pic_order_cnt_lsb_minus4,
              sps.log2_max_frame_num_minus4);

  int shown = 0;
  for (auto& nal : nals) {
    if (nal.nal_type < 1 || nal.nal_type > 5) continue;
    H264SliceHeader header;
    const bool ok =
        ParseH264SliceHeader(nal, parser.NalData(nal), sps, header);
    std::printf("  nal_type=%u ok=%d first_mb=%u frame_num=%u poc=%u\n",
                nal.nal_type, ok, header.first_mb_in_slice, header.frame_num,
                header.pic_order_cnt_lsb);
    if (++shown >= 15) break;
  }
  return 0;
}
