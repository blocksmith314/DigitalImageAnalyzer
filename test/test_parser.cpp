#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "bitstreamparser.h"
#include "h264types.h"
#include "h265types.h"
#include "nalanalyzer.h"

namespace {

std::string sample_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool file_exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

const NALUnit* firstOfType(const std::vector<NALUnit>& nals, CodecType codec,
                           uint8_t type) {
  for (const auto& nal : nals) {
    if (nal.codec == codec && nal.nal_type == type) return &nal;
  }
  return nullptr;
}

}  // namespace

TEST(BitstreamParserTest, ParsesH264Sample) {
  if (!file_exists(sample_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path("output.h264")))
      << parser.LastError();

  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 169u);
  ASSERT_GE(nals.size(), 5u);
  EXPECT_EQ(nals[0].nal_type, 7);  // SPS
  EXPECT_EQ(nals[1].nal_type, 8);  // PPS
  EXPECT_EQ(nals[2].nal_type, 6);  // SEI
  EXPECT_EQ(nals[3].nal_type, 5);  // IDR
  EXPECT_EQ(nals[4].nal_type, 1);  // non-IDR slice

  for (size_t i = 0; i < nals.size(); i++) {
    EXPECT_EQ(nals[i].codec, CodecType::kH264);
    EXPECT_GT(nals[i].size, 0u);
    EXPECT_NE(parser.NalData(nals[i]), nullptr);
    if (i > 0) EXPECT_GT(nals[i].offset, nals[i - 1].offset);
  }
}

TEST(BitstreamParserTest, ParsesH264Sps) {
  if (!file_exists(sample_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path("output.h264")))
      << parser.LastError();

  const NALUnit* spsNal = firstOfType(parser.NalUnits(), CodecType::kH264, 7);
  ASSERT_NE(spsNal, nullptr);

  H264SPS sps;
  ASSERT_TRUE(ParseH264Sps(*spsNal, parser.NalData(*spsNal), sps));
  EXPECT_EQ(sps.width, 352u);
  EXPECT_EQ(sps.height, 288u);
  EXPECT_EQ(sps.profile_idc, 100u);
  EXPECT_EQ(sps.level_idc, 20u);
  EXPECT_EQ(sps.chroma_format_idc, 1u);  // 4:2:0
  EXPECT_EQ(sps.sps_id, 0u);
  EXPECT_TRUE(sps.frame_mbs_only_flag);
}

TEST(NALAnalyzerTest, GroupsH264Frames) {
  if (!file_exists(sample_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path("output.h264")))
      << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));
  const StreamStats& stats = analyzer.stats();

  EXPECT_EQ(stats.FrameCount, 120u);
  EXPECT_EQ(stats.i_frame_count + stats.p_frame_count + stats.b_frame_count,
            stats.FrameCount);
  EXPECT_EQ(stats.total_nals, 169u);
  EXPECT_EQ(stats.width, 352u);
  EXPECT_EQ(stats.height, 288u);
  EXPECT_FALSE(analyzer.H264SpsMap().empty());
  EXPECT_FALSE(analyzer.H264PpsMap().empty());
}

TEST(BitstreamParserTest, ParsesH265Sample) {
  if (!file_exists(sample_path("output.h265")))
    GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH265);
  ASSERT_TRUE(parser.ParseFile(sample_path("output.h265")))
      << parser.LastError();

  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 216u);

  int vps = 0, sps = 0, pps = 0;
  for (const auto& nal : nals) {
    EXPECT_EQ(nal.codec, CodecType::kH265);
    if (nal.nal_type == 32) vps++;
    if (nal.nal_type == 33) sps++;
    if (nal.nal_type == 34) pps++;
  }
  EXPECT_EQ(vps, 24);
  EXPECT_EQ(sps, 24);
  EXPECT_EQ(pps, 24);

  const NALUnit* spsNal = firstOfType(nals, CodecType::kH265, 33);
  ASSERT_NE(spsNal, nullptr);
  H265SPS hevc_sps;
  ASSERT_TRUE(ParseH265Sps(*spsNal, parser.NalData(*spsNal), hevc_sps));
  EXPECT_EQ(hevc_sps.width, 352u);
  EXPECT_EQ(hevc_sps.height, 288u);
  EXPECT_EQ(hevc_sps.profile_idc, 1u);  // Main
  EXPECT_EQ(hevc_sps.level_idc, 60u);
}
