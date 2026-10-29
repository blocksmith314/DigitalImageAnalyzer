#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "h264types.h"
#include "h265types.h"
#include "nalanalyzer.h"

#include <fstream>
#include <string>
#include <vector>

namespace
{

    std::string samplePath(const char* name) { return std::string(TEST_DATA_DIR) + "/" + name; }

    bool fileExists(const std::string& path)
    {
        std::ifstream file(path);
        return file.good();
    }

    const NALUnit* firstOfType(const std::vector<NALUnit>& nals, CodecType codec, uint8_t type)
    {
        for (const auto& nal : nals)
        {
            if (nal.codec == codec && nal.nalType == type)
                return &nal;
        }
        return nullptr;
    }

} // namespace

TEST(BitstreamParserTest, ParsesH264Sample)
{
    if (!fileExists(samplePath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath("output.h264"))) << parser.lastError();

    const auto& nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 169u);
    ASSERT_GE(nals.size(), 5u);
    EXPECT_EQ(nals[0].nalType, 7); // SPS
    EXPECT_EQ(nals[1].nalType, 8); // PPS
    EXPECT_EQ(nals[2].nalType, 6); // SEI
    EXPECT_EQ(nals[3].nalType, 5); // IDR
    EXPECT_EQ(nals[4].nalType, 1); // non-IDR slice

    for (size_t i = 0; i < nals.size(); i++)
    {
        EXPECT_EQ(nals[i].codec, CodecType::H264);
        EXPECT_GT(nals[i].size, 0u);
        EXPECT_NE(parser.nalData(nals[i]), nullptr);
        if (i > 0)
            EXPECT_GT(nals[i].offset, nals[i - 1].offset);
    }
}

TEST(BitstreamParserTest, ParsesH264Sps)
{
    if (!fileExists(samplePath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath("output.h264"))) << parser.lastError();

    const NALUnit* spsNal = firstOfType(parser.nalUnits(), CodecType::H264, 7);
    ASSERT_NE(spsNal, nullptr);

    H264SPS sps;
    ASSERT_TRUE(parseH264SPS(*spsNal, parser.nalData(*spsNal), sps));
    EXPECT_EQ(sps.width, 352u);
    EXPECT_EQ(sps.height, 288u);
    EXPECT_EQ(sps.profile_idc, 100u);
    EXPECT_EQ(sps.level_idc, 20u);
    EXPECT_EQ(sps.chroma_format_idc, 1u); // 4:2:0
    EXPECT_EQ(sps.sps_id, 0u);
    EXPECT_TRUE(sps.frame_mbs_only_flag);
}

TEST(NALAnalyzerTest, GroupsH264Frames)
{
    if (!fileExists(samplePath("output.h264")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath("output.h264"))) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));
    const StreamStats& stats = analyzer.stats();

    EXPECT_EQ(stats.frameCount, 120u);
    EXPECT_EQ(stats.iFrameCount + stats.pFrameCount + stats.bFrameCount, stats.frameCount);
    EXPECT_EQ(stats.totalNals, 169u);
    EXPECT_EQ(stats.width, 352u);
    EXPECT_EQ(stats.height, 288u);
    EXPECT_FALSE(analyzer.h264SpsMap().empty());
    EXPECT_FALSE(analyzer.h264PpsMap().empty());
}

TEST(BitstreamParserTest, ParsesH265Sample)
{
    if (!fileExists(samplePath("output.h265")))
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseFile(samplePath("output.h265"))) << parser.lastError();

    const auto& nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 216u);

    int vps = 0, sps = 0, pps = 0;
    for (const auto& nal : nals)
    {
        EXPECT_EQ(nal.codec, CodecType::H265);
        if (nal.nalType == 32)
            vps++;
        if (nal.nalType == 33)
            sps++;
        if (nal.nalType == 34)
            pps++;
    }
    EXPECT_EQ(vps, 24);
    EXPECT_EQ(sps, 24);
    EXPECT_EQ(pps, 24);

    const NALUnit* spsNal = firstOfType(nals, CodecType::H265, 33);
    ASSERT_NE(spsNal, nullptr);
    H265SPS hevcSps;
    ASSERT_TRUE(parseH265SPS(*spsNal, parser.nalData(*spsNal), hevcSps));
    EXPECT_EQ(hevcSps.width, 352u);
    EXPECT_EQ(hevcSps.height, 288u);
    EXPECT_EQ(hevcSps.profile_idc, 1u); // Main
    EXPECT_EQ(hevcSps.level_idc, 60u);
}
