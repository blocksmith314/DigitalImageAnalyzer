#include <gtest/gtest.h>

#include "annexb.h"
#include "bitstreamparser.h"

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

TEST(ProbeTest, DetectsRawAnnexB) {
    if (!exists(dataPath("output.h264"))) GTEST_SKIP() << "sample stream missing";

    CodecType codec = CodecType::H265;
    bool raw = false;
    std::string error;
    ASSERT_TRUE(probeVideoFile(dataPath("output.h264"), codec, raw, error)) << error;
    EXPECT_TRUE(raw);
    EXPECT_EQ(codec, CodecType::H264);
}

TEST(ProbeTest, DetectsContainer) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    CodecType codec = CodecType::H265;
    bool raw = true;
    std::string error;
    ASSERT_TRUE(probeVideoFile(dataPath("sample_720p.mp4"), codec, raw, error)) << error;
    EXPECT_FALSE(raw);
    EXPECT_EQ(codec, CodecType::H264);
}

TEST(ProbeTest, ReportsMissingFile) {
    CodecType codec = CodecType::H264;
    bool raw = false;
    std::string error;
    EXPECT_FALSE(probeVideoFile(dataPath("does-not-exist.mp4"), codec, raw, error));
    EXPECT_FALSE(error.empty());
}

TEST(AvccParserTest, ParsesMp4VideoTrack) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    BitstreamParser parser(CodecType::H264);
    CodecType codec = CodecType::H265;
    ASSERT_TRUE(parser.parseAvcc(dataPath("sample_720p.mp4"), codec)) << parser.lastError();

    EXPECT_EQ(codec, CodecType::H264);
    const auto &nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 13u);
    EXPECT_EQ(nals[0].nalType, 7);
    EXPECT_EQ(nals[1].nalType, 8);
    EXPECT_EQ(nals[2].nalType, 6);
    EXPECT_EQ(nals[3].nalType, 5);
    for (const auto &nal : nals) {
        EXPECT_GT(nal.size, 0u);
        EXPECT_NE(parser.nalData(nal), nullptr);
    }
}
