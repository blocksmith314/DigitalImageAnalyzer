#include <gtest/gtest.h>

#include "audiodecoder.h"

#include <fstream>
#include <string>
#include <vector>

namespace {

std::string dataPath(const char *name) {
    return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

} // namespace

TEST(AudioDecoderTest, OpensAudioStream) {
    if (!exists(dataPath("sample_av.mp4"))) GTEST_SKIP() << "audio fixture missing";

    AudioDecoder decoder;
    ASSERT_TRUE(decoder.open(dataPath("sample_av.mp4")));
    EXPECT_TRUE(decoder.hasAudio());
    EXPECT_EQ(decoder.sourceSampleRate(), 48000);
    EXPECT_EQ(decoder.sourceChannels(), 1);
}

TEST(AudioDecoderTest, RejectsVideoOnlyFile) {
    if (!exists(dataPath("output.h264"))) GTEST_SKIP() << "sample stream missing";

    AudioDecoder decoder;
    EXPECT_FALSE(decoder.open(dataPath("output.h264")));
    EXPECT_FALSE(decoder.hasAudio());
}

TEST(AudioDecoderTest, DecodesOneSecondOfPcm) {
    if (!exists(dataPath("sample_av.mp4"))) GTEST_SKIP() << "audio fixture missing";

    AudioDecoder decoder;
    ASSERT_TRUE(decoder.open(dataPath("sample_av.mp4")));
    decoder.setOutputFormat(48000, 2, PcmFormat::S16);

    std::vector<uint8_t> buffer(8192);
    long long total = 0;
    for (int guard = 0; guard < 1000; guard++) {
        const int n = decoder.decode(buffer.data(), (int)buffer.size());
        if (n <= 0) break;
        total += n;
    }
    // 1 s of 48 kHz stereo S16 is 192000 bytes; AAC priming adds a little.
    EXPECT_GT(total, 150000);
    EXPECT_LT(total, 240000);
}

TEST(AudioDecoderTest, SeekRestartsDecoding) {
    if (!exists(dataPath("sample_av.mp4"))) GTEST_SKIP() << "audio fixture missing";

    AudioDecoder decoder;
    ASSERT_TRUE(decoder.open(dataPath("sample_av.mp4")));
    decoder.setOutputFormat(48000, 2, PcmFormat::S16);

    std::vector<uint8_t> buffer(4096);
    ASSERT_GT(decoder.decode(buffer.data(), (int)buffer.size()), 0);

    decoder.seekTo(0.5);
    EXPECT_GT(decoder.decode(buffer.data(), (int)buffer.size()), 0);
}
