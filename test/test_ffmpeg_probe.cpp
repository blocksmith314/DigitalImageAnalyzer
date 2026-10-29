#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "annexb.h"
#include "bitstreamparser.h"

namespace {

std::string data_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

}  // namespace

TEST(ProbeTest, DetectsRawAnnexB) {
  if (!exists(data_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  CodecType codec = CodecType::kH265;
  bool raw = false;
  std::string error;
  ASSERT_TRUE(ProbeVideoFile(data_path("output.h264"), codec, raw, error))
      << error;
  EXPECT_TRUE(raw);
  EXPECT_EQ(codec, CodecType::kH264);
}

TEST(ProbeTest, DetectsContainer) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  CodecType codec = CodecType::kH265;
  bool raw = true;
  std::string error;
  ASSERT_TRUE(ProbeVideoFile(data_path("sample_720p.mp4"), codec, raw, error))
      << error;
  EXPECT_FALSE(raw);
  EXPECT_EQ(codec, CodecType::kH264);
}

TEST(ProbeTest, ReportsMissingFile) {
  CodecType codec = CodecType::kH264;
  bool raw = false;
  std::string error;
  EXPECT_FALSE(
      ProbeVideoFile(data_path("does-not-exist.mp4"), codec, raw, error));
  EXPECT_FALSE(error.empty());
}

TEST(AvccParserTest, ParsesMp4VideoTrack) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  BitstreamParser parser(CodecType::kH264);
  CodecType codec = CodecType::kH265;
  ASSERT_TRUE(parser.ParseAvcc(data_path("sample_720p.mp4"), codec))
      << parser.LastError();

  EXPECT_EQ(codec, CodecType::kH264);
  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 13u);
  EXPECT_EQ(nals[0].nal_type, 7);
  EXPECT_EQ(nals[1].nal_type, 8);
  EXPECT_EQ(nals[2].nal_type, 6);
  EXPECT_EQ(nals[3].nal_type, 5);
  for (const auto& nal : nals) {
    EXPECT_GT(nal.size, 0u);
    EXPECT_NE(parser.NalData(nal), nullptr);
  }
}
