#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "nalanalyzer.h"

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

TEST(Mp4IndexTest, ParsesStcoSampleTable) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    BitstreamParser parser(CodecType::H264);
    CodecType codec = CodecType::H264;
    ASSERT_TRUE(parser.parseMp4Indexed(dataPath("sample_720p.mp4"), codec))
        << parser.lastError();

    EXPECT_EQ(codec, CodecType::H264);
    const auto &nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 13u);   // SPS, PPS, SEI, IDR + 9 slices
    EXPECT_EQ(nals[0].nalType, 7);
    EXPECT_EQ(nals[1].nalType, 8);
    EXPECT_EQ(nals[2].nalType, 6);
    EXPECT_EQ(nals[3].nalType, 5);

    for (const auto &nal : nals) {
        EXPECT_GT(nal.size, 0u);
        EXPECT_NE(parser.nalData(nal), nullptr);
    }
}

TEST(Mp4IndexTest, GroupsFramesAfterIndexImport) {
    if (!exists(dataPath("sample_720p.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    BitstreamParser parser(CodecType::H264);
    CodecType codec = CodecType::H264;
    ASSERT_TRUE(parser.parseMp4Indexed(dataPath("sample_720p.mp4"), codec))
        << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));
    EXPECT_EQ(analyzer.stats().frameCount, 10u);   // 1 s at 10 fps
    EXPECT_EQ(analyzer.stats().width, 1280u);
    EXPECT_EQ(analyzer.stats().height, 720u);
}

TEST(Mp4IndexTest, ReportsDenseTableForFallback) {
    if (!exists(dataPath("sample.mp4"))) GTEST_SKIP() << "MP4 fixture missing";

    BitstreamParser parser(CodecType::H264);
    CodecType codec = CodecType::H264;
    EXPECT_FALSE(parser.parseMp4Indexed(dataPath("sample.mp4"), codec));
    EXPECT_NE(parser.lastError().find("Dense sample table"), std::string::npos);
}

TEST(Mp4IndexTest, RejectsAnnexBStream) {
    if (!exists(dataPath("output.h264"))) GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    CodecType codec = CodecType::H264;
    EXPECT_FALSE(parser.parseMp4Indexed(dataPath("output.h264"), codec));
    EXPECT_NE(parser.lastError().find("Not an MP4"), std::string::npos);
}
