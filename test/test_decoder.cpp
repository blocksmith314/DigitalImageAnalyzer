#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "bitstreamparser.h"
#include "decoder.h"
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

TEST(DecoderTest, OpensMp4) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();
  EXPECT_NEAR(decoder.FrameRate(), 10.0, 0.01);
  EXPECT_EQ(decoder.width(), 1280);
  EXPECT_EQ(decoder.height(), 720);
  EXPECT_TRUE(decoder.HasTimestamps());
}

TEST(DecoderTest, DecodesFirstFrame) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();

  DecodedFrame frame;
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  ASSERT_FALSE(frame.image.isNull());
  EXPECT_EQ(frame.image.width(), 1280);
  EXPECT_EQ(frame.image.height(), 720);
  // BGRA output avoids the unaccelerated yuv420p -> rgb24 swscale path.
  EXPECT_EQ(frame.image.format(), QImage::Format_RGB32);
  EXPECT_NEAR(frame.pts_sec, 0.0, 0.001);
}

TEST(DecoderTest, SeeksToFrameTimestamp) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

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
  if (!exists(data_path("output.h264"))) {
    GTEST_SKIP() << "sample stream missing";
  }

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

TEST(DecoderTest, ForwardSmallSeekDecodesOnlyTheGap) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();

  DecodedFrame first;
  ASSERT_TRUE(decoder.DecodeNextFrame(first));

  ASSERT_TRUE(decoder.SeekToFrame(5));
  const auto stats = decoder.LastSeekStats();
  EXPECT_FALSE(stats.used_container);
  EXPECT_EQ(stats.frames_decoded, 4);

  DecodedFrame frame;
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  EXPECT_NEAR(frame.pts_sec, 0.5, 0.01);  // 10 fps
}

TEST(DecoderTest, BackwardSeekUsesContainerSeek) {
  if (!exists(data_path("sample_720p.mp4"))) {
    GTEST_SKIP() << "MP4 fixture missing";
  }

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(data_path("sample_720p.mp4"), CodecType::kH264))
      << decoder.LastError();

  DecodedFrame frame;
  for (int i = 0; i < 6; i++) {
    ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  }

  ASSERT_TRUE(decoder.SeekToFrame(2));
  EXPECT_TRUE(decoder.LastSeekStats().used_container);

  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  EXPECT_NEAR(frame.pts_sec, 0.2, 0.01);
}

TEST(DecoderTest, RawSeekJumpsToPreviousIdr) {
  const std::string path = data_path("output.h264");
  if (!exists(path)) {
    GTEST_SKIP() << "raw fixture missing";
  }

  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(path)) << parser.LastError();
  NALAnalyzer analyzer;
  ASSERT_TRUE(analyzer.AnalyzeAll(parser));

  std::vector<FFmpegDecoder::RawKeyframe> keyframes;
  int64_t parameter_offset = -1;
  for (const NALUnit& nal : parser.NalUnits()) {
    if (nal.nal_type == 7 || nal.nal_type == 8) {
      if (parameter_offset < 0) {
        parameter_offset = (int64_t)nal.offset;
      }
    } else if (nal.nal_type == 5 && nal.frame_index >= 0) {
      keyframes.push_back({nal.frame_index, parameter_offset >= 0
                                                ? parameter_offset
                                                : (int64_t)nal.offset});
      parameter_offset = -1;
    }
  }
  ASSERT_FALSE(keyframes.empty());

  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.OpenFile(path, CodecType::kH264)) << decoder.LastError();
  decoder.SetRawKeyframeIndex(keyframes);

  DecodedFrame frame;
  QImage expected;
  for (int i = 0; i <= 50; i++) {
    ASSERT_TRUE(decoder.DecodeNextFrame(frame));
    if (i == 22) {
      expected = frame.image;
    }
  }
  ASSERT_FALSE(expected.isNull());

  ASSERT_TRUE(decoder.SeekToFrame(22));
  EXPECT_TRUE(decoder.LastSeekStats().used_raw_keyframe);
  ASSERT_TRUE(decoder.DecodeNextFrame(frame));
  EXPECT_EQ(frame.image, expected);
}
