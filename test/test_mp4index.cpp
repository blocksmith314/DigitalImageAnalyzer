#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "bitstreamparser.h"
#include "nalanalyzer.h"

namespace {

std::string data_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

}  // namespace

TEST(Mp4IndexTest, ParsesStcoSampleTable) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  BitstreamParser parser(CodecType::kH264);
  CodecType codec = CodecType::kH264;
  ASSERT_TRUE(parser.ParseMp4Indexed(data_path("sample_720p.mp4"), codec))
      << parser.LastError();

  EXPECT_EQ(codec, CodecType::kH264);
  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 13u);  // SPS, PPS, SEI, IDR + 9 slices
  EXPECT_EQ(nals[0].nal_type, 7);
  EXPECT_EQ(nals[1].nal_type, 8);
  EXPECT_EQ(nals[2].nal_type, 6);
  EXPECT_EQ(nals[3].nal_type, 5);

  for (const auto& nal : nals) {
    EXPECT_GT(nal.size, 0u);
    EXPECT_NE(parser.NalData(nal), nullptr);
  }
}

TEST(Mp4IndexTest, GroupsFramesAfterIndexImport) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  BitstreamParser parser(CodecType::kH264);
  CodecType codec = CodecType::kH264;
  ASSERT_TRUE(parser.ParseMp4Indexed(data_path("sample_720p.mp4"), codec))
      << parser.LastError();

  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));
  EXPECT_EQ(analyzer.stats().FrameCount, 10u);  // 1 s at 10 fps
  EXPECT_EQ(analyzer.stats().width, 1280u);
  EXPECT_EQ(analyzer.stats().height, 720u);
}

TEST(Mp4IndexTest, ReportsDenseTableForFallback) {
  if (!exists(data_path("sample.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

  BitstreamParser parser(CodecType::kH264);
  CodecType codec = CodecType::kH264;
  EXPECT_FALSE(parser.ParseMp4Indexed(data_path("sample.mp4"), codec));
  EXPECT_NE(parser.LastError().find("Dense sample table"), std::string::npos);
}

TEST(Mp4IndexTest, RejectsAnnexBStream) {
  if (!exists(data_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH264);
  CodecType codec = CodecType::kH264;
  EXPECT_FALSE(parser.ParseMp4Indexed(data_path("output.h264"), codec));
  EXPECT_NE(parser.LastError().find("Not an MP4"), std::string::npos);
}
