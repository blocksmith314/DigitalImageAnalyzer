#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "audiodecoder.h"

namespace {

std::string data_path(const char* name) {
  return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

}  // namespace

TEST(AudioDecoderTest, OpensAudioStream) {
  if (!exists(data_path("sample_av.mp4")))
    GTEST_SKIP() << "audio fixture missing";

  AudioDecoder decoder;
  ASSERT_TRUE(decoder.open(data_path("sample_av.mp4")));
  EXPECT_TRUE(decoder.HasAudio());
  EXPECT_EQ(decoder.SourceSampleRate(), 48000);
  EXPECT_EQ(decoder.SourceChannels(), 1);
}

TEST(AudioDecoderTest, RejectsVideoOnlyFile) {
  if (!exists(data_path("output.h264")))
    GTEST_SKIP() << "sample stream missing";

  AudioDecoder decoder;
  EXPECT_FALSE(decoder.open(data_path("output.h264")));
  EXPECT_FALSE(decoder.HasAudio());
}

TEST(AudioDecoderTest, DecodesOneSecondOfPcm) {
  if (!exists(data_path("sample_av.mp4")))
    GTEST_SKIP() << "audio fixture missing";

  AudioDecoder decoder;
  ASSERT_TRUE(decoder.open(data_path("sample_av.mp4")));
  decoder.SetOutputFormat(48000, 2, PcmFormat::kS16);

  std::vector<uint8_t> buffer(8192);
  long long total = 0;
  for (int guard = 0; guard < 1000; guard++) {
    const int n = decoder.decode(buffer.data(), (int)buffer.size());
    if (n <= 0) break;
    total += n;
  }
  // 1 s of 48 kHz stereo kS16 is 192000 bytes; AAC priming adds a little.
  EXPECT_GT(total, 150000);
  EXPECT_LT(total, 240000);
}

TEST(AudioDecoderTest, SeekRestartsDecoding) {
  if (!exists(data_path("sample_av.mp4")))
    GTEST_SKIP() << "audio fixture missing";

  AudioDecoder decoder;
  ASSERT_TRUE(decoder.open(data_path("sample_av.mp4")));
  decoder.SetOutputFormat(48000, 2, PcmFormat::kS16);

  std::vector<uint8_t> buffer(4096);
  ASSERT_GT(decoder.decode(buffer.data(), (int)buffer.size()), 0);

  decoder.SeekTo(0.5);
  EXPECT_GT(decoder.decode(buffer.data(), (int)buffer.size()), 0);
}
