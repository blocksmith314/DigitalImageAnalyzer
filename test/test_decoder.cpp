#include <gtest/gtest.h>

#include "decoder.h"

#include <fstream>
#include <string>

namespace {

std::string dataPath(const char *name) {
    return std::string(TEST_DATA_DIR) + "/" + name;
}

bool exists(const std::string &path) {
    std::ifstream file(path);
    return file.good();
}

} // namespace

TEST(DecoderTest, OpensMp4) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    FFmpegDecoder decoder;
    ASSERT_TRUE(decoder.openFile(dataPath("sample_720p.mp4"), CodecType::H264))
        << decoder.lastError();
    EXPECT_NEAR(decoder.frameRate(), 10.0, 0.01);
    EXPECT_EQ(decoder.width(), 1280);
    EXPECT_EQ(decoder.height(), 720);
    EXPECT_TRUE(decoder.hasTimestamps());
}

TEST(DecoderTest, DecodesFirstFrame) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    FFmpegDecoder decoder;
    ASSERT_TRUE(decoder.openFile(dataPath("sample_720p.mp4"), CodecType::H264))
        << decoder.lastError();

    DecodedFrame frame;
    ASSERT_TRUE(decoder.decodeNextFrame(frame));
    ASSERT_FALSE(frame.image.isNull());
    EXPECT_EQ(frame.image.width(), 1280);
    EXPECT_EQ(frame.image.height(), 720);
    EXPECT_NEAR(frame.ptsSec, 0.0, 0.001);
}

TEST(DecoderTest, SeeksToFrameTimestamp) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    FFmpegDecoder decoder;
    ASSERT_TRUE(decoder.openFile(dataPath("sample_720p.mp4"), CodecType::H264))
        << decoder.lastError();

    ASSERT_TRUE(decoder.seekToFrame(5));
    DecodedFrame frame;
    ASSERT_TRUE(decoder.decodeNextFrame(frame));
    ASSERT_FALSE(frame.image.isNull());
    EXPECT_NEAR(frame.ptsSec, 0.5, 0.01);   // 10 fps
}

TEST(DecoderTest, RawStreamHasNoTimestamps) {
    if (!exists(dataPath("output.h264"))) GTEST_SKIP() << "sample stream missing";

    FFmpegDecoder decoder;
    ASSERT_TRUE(decoder.openFile(dataPath("output.h264"), CodecType::H264))
        << decoder.lastError();
    EXPECT_FALSE(decoder.hasTimestamps());

    DecodedFrame frame;
    ASSERT_TRUE(decoder.decodeNextFrame(frame));
    ASSERT_FALSE(frame.image.isNull());
    EXPECT_EQ(frame.image.width(), 352);
    EXPECT_EQ(frame.image.height(), 288);
}
