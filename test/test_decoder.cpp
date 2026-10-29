#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "decoder.h"

namespace {

std::string data_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

}  // namespace

TEST(DecoderTest, OpensMp4) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();
  EXPECT_NEAR(decoder.FrameRate(), 10.0, 0.01);
  EXPECT_EQ(decoder.width(), 1280);
  EXPECT_EQ(decoder.height(), 720);
  EXPECT_TRUE(decoder.HasTimestamps());
}

TEST(DecoderTest, DecodesFirstFrame) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();

  DecodedFrame frame;
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  ASSERT_FALSE(frame.image.isNull());
  EXPECT_EQ(frame.image.width(), 1280);
  EXPECT_EQ(frame.image.height(), 720);
  EXPECT_NEAR(frame.pts_sec, 0.0, 0.001);
}

TEST(DecoderTest, SeeksToFrameTimestamp) {
  if (!exists(data_path("sample_720p.mp4")))
    GTEST_SKIP() << "MP4 fixture missing";

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();

  ASSERT_TRUE(decoder.SeekToFrame(5));
  DecodedFrame frame;
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  ASSERT_FALSE(frame.image.isNull());
  EXPECT_NEAR(frame.pts_sec, 0.5, 0.01);  // 10 fps
}

TEST(DecoderTest, RawStreamHasNoTimestamps) {
  if (!exists(data_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("output.h264"), CodecType::kH264))
      << decoder.LastError();
  EXPECT_FALSE(decoder.HasTimestamps());

  DecodedFrame frame;
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  ASSERT_FALSE(frame.image.isNull());
  EXPECT_EQ(frame.image.width(), 352);
  EXPECT_EQ(frame.image.height(), 288);
}
