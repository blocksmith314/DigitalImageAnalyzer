#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "h264types.h"
#include "nalanalyzer.h"

#include <fstream>
#include <string>

namespace
{

    std::string samplePath() { return std::string(TEST_DATA_DIR) + "/output.h264"; }
    bool sampleExists()
    {
        std::ifstream file(samplePath());
        return file.good();
    }

    const NALUnit* firstOfType(const std::vector<NALUnit>& nals, uint8_t type)
    {
        for (const auto& nal : nals)
        {
            if (nal.nalType == type)
                return &nal;
        }
        return nullptr;
    }

    NALAnalyzer analyzerFor(BitstreamParser& parser)
    {
        NALAnalyzer analyzer;
        analyzer.analyzeAll(parser);
        return analyzer;
    }

} // namespace

TEST(H264ParamsTest, ParsesSpsDetails)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit* nal = firstOfType(parser.nalUnits(), 7);
    ASSERT_NE(nal, nullptr);
    H264SPS sps;
    ASSERT_TRUE(parseH264SPS(*nal, parser.nalData(*nal), sps));

    EXPECT_EQ(sps.profileName(), "High");
    EXPECT_EQ(sps.pic_order_cnt_type, 0u);
    EXPECT_EQ(sps.max_num_ref_frames, 4u);
    EXPECT_TRUE(sps.frame_mbs_only_flag);
}

TEST(H264ParamsTest, ParsesPpsDetails)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit* nal = firstOfType(parser.nalUnits(), 8);
    ASSERT_NE(nal, nullptr);
    H264PPS pps;
    ASSERT_TRUE(parseH264PPS(*nal, parser.nalData(*nal), pps));

    EXPECT_EQ(pps.pps_id, 0u);
    EXPECT_EQ(pps.sps_id, 0u);
    EXPECT_EQ(pps.num_slice_groups_minus1, 0u);
    EXPECT_TRUE(pps.entropy_coding_mode_flag); // CABAC
    EXPECT_TRUE(pps.transform_8x8_mode_flag);
    EXPECT_TRUE(pps.deblocking_filter_control_present_flag);
}

TEST(H264ParamsTest, CountsParameterSetsAndSlices)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));
    const StreamStats& stats = analyzer.stats();

    EXPECT_EQ(stats.totalNals, 169u);
    EXPECT_EQ(stats.spsCount, 24u);
    EXPECT_EQ(stats.ppsCount, 24u);
    EXPECT_EQ(stats.seiCount, 1u);
    EXPECT_EQ(stats.audCount, 0u);
    EXPECT_EQ(stats.sliceCount, 120u);
    EXPECT_EQ(stats.idrCount, 24u);
    EXPECT_EQ(stats.frameCount, 120u);
}

TEST(H264ParamsTest, ParsesSliceHeadersInPocOrder)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit* spsNal = firstOfType(parser.nalUnits(), 7);
    ASSERT_NE(spsNal, nullptr);
    H264SPS sps;
    ASSERT_TRUE(parseH264SPS(*spsNal, parser.nalData(*spsNal), sps));

    struct Expected
    {
        uint32_t nalType;
        uint32_t frameNum;
        uint32_t pocLsb;
    };
    const Expected expected[] = {
        {5, 0, 0}, // IDR
        {1, 1, 2},
        {1, 2, 8},
    };

    size_t index = 0;
    for (const auto& nal : parser.nalUnits())
    {
        if (nal.nalType < 1 || nal.nalType > 5)
            continue;
        if (index >= std::size(expected))
            break;

        H264SliceHeader header;
        ASSERT_TRUE(parseH264SliceHeader(nal, parser.nalData(nal), sps, header));
        EXPECT_EQ(nal.nalType, expected[index].nalType);
        EXPECT_EQ(header.frame_num, expected[index].frameNum);
        EXPECT_EQ(header.pic_order_cnt_lsb, expected[index].pocLsb);
        index++;
    }
    EXPECT_EQ(index, std::size(expected));
}

TEST(H264ParamsTest, AssignsDecodeAndDisplayOrder)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    NALAnalyzer analyzer;
    ASSERT_TRUE(analyzer.analyzeAll(parser));

    int reordered = 0;
    for (const auto& nal : parser.nalUnits())
    {
        if (nal.nalType < 1 || nal.nalType > 5)
            continue;
        ASSERT_GE(nal.frameIndex, 0);
        ASSERT_GE(nal.dtsIndex, 0);
        EXPECT_LT(nal.dtsIndex, (int)analyzer.stats().frameCount);
        if (nal.dtsIndex != nal.frameIndex)
            reordered++;
    }
    EXPECT_GT(reordered, 0); // the sample contains B frames
}

TEST(H264ParamsTest, BuildsSpsDetailsText)
{
    if (!sampleExists())
        GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit* spsNal = firstOfType(parser.nalUnits(), 7);
    ASSERT_NE(spsNal, nullptr);
    const NALAnalysis analysis = analyzerFor(parser).analyzeOne(*spsNal, parser.nalData(*spsNal));
    EXPECT_NE(analysis.details.find("SPS Details"), std::string::npos);
    EXPECT_NE(analysis.details.find("Profile: High"), std::string::npos);
}
