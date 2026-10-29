#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "bitstreamparser.h"
#include "h264types.h"
#include "nalanalyzer.h"

namespace {

std::string sample_path() {
  return std::string(TEST_DATA_DIR) + "/output.h264";
}
bool sample_exists() {
  std::ifstream file(sample_path());
  return file.good();
}

const NALUnit* firstOfType(const std::vector<NALUnit>& nals, uint8_t type) {
  for (const auto& nal : nals) {
    if (nal.nal_type == type) return &nal;
  }
  return nullptr;
}

NALAnalyzer analyzerFor(BitstreamParser& parser) {
  NALAnalyzer analyzer;
  analyzer.AnalyzeAll(parser);
  return analyzer;
}

}  // namespace

TEST(H264ParamsTest, ParsesSpsDetails) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* nal = firstOfType(parser.NalUnits(), 7);
  ASSERT_NE(nal, nullptr);
  H264SPS sps;
  ASSERT_TRUE(ParseH264Sps(*nal, parser.NalData(*nal), sps));

  EXPECT_EQ(sps.ProfileName(), "High");
  EXPECT_EQ(sps.pic_order_cnt_type, 0u);
  EXPECT_EQ(sps.max_num_ref_frames, 4u);
  EXPECT_TRUE(sps.frame_mbs_only_flag);
}

TEST(H264ParamsTest, ParsesPpsDetails) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* nal = firstOfType(parser.NalUnits(), 8);
  ASSERT_NE(nal, nullptr);
  H264PPS pps;
  ASSERT_TRUE(ParseH264Pps(*nal, parser.NalData(*nal), pps));

  EXPECT_EQ(pps.pps_id, 0u);
  EXPECT_EQ(pps.sps_id, 0u);
  EXPECT_EQ(pps.num_slice_groups_minus1, 0u);
  EXPECT_TRUE(pps.entropy_coding_mode_flag);  // CABAC
  EXPECT_TRUE(pps.transform_8x8_mode_flag);
  EXPECT_TRUE(pps.deblocking_filter_control_present_flag);
}

TEST(H264ParamsTest, CountsParameterSetsAndSlices) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));
  const StreamStats& stats = analyzer.stats();

  EXPECT_EQ(stats.total_nals, 169u);
  EXPECT_EQ(stats.sps_count, 24u);
  EXPECT_EQ(stats.pps_count, 24u);
  EXPECT_EQ(stats.sei_count, 1u);
  EXPECT_EQ(stats.aud_count, 0u);
  EXPECT_EQ(stats.slice_count, 120u);
  EXPECT_EQ(stats.idr_count, 24u);
  EXPECT_EQ(stats.FrameCount, 120u);
}

TEST(H264ParamsTest, ParsesSliceHeadersInPocOrder) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* spsNal = firstOfType(parser.NalUnits(), 7);
  ASSERT_NE(spsNal, nullptr);
  H264SPS sps;
  ASSERT_TRUE(ParseH264Sps(*spsNal, parser.NalData(*spsNal), sps));

  struct Expected {
    uint32_t nal_type;
    uint32_t frame_num;
    uint32_t poc_lsb;
  };
  const Expected expected[] = {
      {5, 0, 0},  // IDR
      {1, 1, 2},
      {1, 2, 8},
  };

  size_t index = 0;
  for (const auto& nal : parser.NalUnits()) {
    if (nal.nal_type < 1 || nal.nal_type > 5) continue;
    if (index >= std::size(expected)) break;

    H264SliceHeader header;
    ASSERT_TRUE(
        ParseH264SliceHeader(nal, parser.NalData(nal), sps, header));
    EXPECT_EQ(nal.nal_type, expected[index].nal_type);
    EXPECT_EQ(header.frame_num, expected[index].frame_num);
    EXPECT_EQ(header.pic_order_cnt_lsb, expected[index].poc_lsb);
    index++;
  }
  EXPECT_EQ(index, std::size(expected));
}

TEST(H264ParamsTest, AssignsDecodeAndDisplayOrder) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));

  int reordered = 0;
  for (const auto& nal : parser.NalUnits()) {
    if (nal.nal_type < 1 || nal.nal_type > 5) continue;
    ASSERT_GE(nal.frame_index, 0);
    ASSERT_GE(nal.dts_index, 0);
    EXPECT_LT(nal.dts_index, (int)analyzer.stats().FrameCount);
    if (nal.dts_index != nal.frame_index) reordered++;
  }
  EXPECT_GT(reordered, 0);  // the sample contains B frames
}

TEST(H264ParamsTest, BuildsSpsDetailsText) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* spsNal = firstOfType(parser.NalUnits(), 7);
  ASSERT_NE(spsNal, nullptr);
  const NALAnalysis analysis =
      analyzerFor(parser).AnalyzeOne(*spsNal, parser.NalData(*spsNal));
  EXPECT_NE(analysis.details.find("SPS Details"), std::string::npos);
  EXPECT_NE(analysis.details.find("Profile: High"), std::string::npos);
}
