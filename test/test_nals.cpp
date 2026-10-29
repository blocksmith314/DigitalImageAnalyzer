#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "nalunit.h"

#include <fstream>
#include <string>
#include <vector>

namespace
{
    const uint8_t kAnnexB[] = {
        0x00, 0x00, 0x01, 0x09, 0xF0, // AUD, 3-byte start code
        0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x00, 0x80, // SEI, 4-byte start code
        0x00, 0x00, 0x01, 0x65, 0x88, // IDR slice
    };
}

TEST(BitstreamParserTest, SplitsAnnexBBuffer)
{
    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseBuffer(kAnnexB, sizeof(kAnnexB)));

    const auto& nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 3u);
    EXPECT_EQ(nals[0].nalType, 9);
    EXPECT_EQ(nals[0].startCodeLen, 3);
    EXPECT_EQ(nals[0].size, 5u);
    EXPECT_EQ(nals[1].nalType, 6);
    EXPECT_EQ(nals[1].startCodeLen, 4);
    EXPECT_EQ(nals[2].nalType, 5);
    EXPECT_EQ(nals[2].startCodeLen, 3);

    EXPECT_EQ(nals[0].offset, 0u);
    EXPECT_EQ(nals[1].offset, 5u);
    EXPECT_EQ(nals[2].offset, 13u);
    EXPECT_NE(parser.nalData(nals[0]), nullptr);
    EXPECT_NE(parser.nalData(nals[2]), nullptr);
}

TEST(BitstreamParserTest, RejectsBufferWithoutStartCode)
{
    const uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};
    BitstreamParser parser(CodecType::H264);
    EXPECT_FALSE(parser.parseBuffer(junk, sizeof(junk)));
    EXPECT_NE(parser.lastError().find("No start code"), std::string::npos);
}

TEST(BitstreamParserTest, RejectsMissingFile)
{
    BitstreamParser parser(CodecType::H264);
    EXPECT_FALSE(parser.parseFile("definitely-missing-stream.h264"));
    EXPECT_FALSE(parser.lastError().empty());
}

TEST(NALUnitTest, NamesH264Types)
{
    NALUnit nal;
    nal.codec = CodecType::H264;
    nal.size = 10;
    nal.offset = 0x2A;

    nal.nalType = 1;
    EXPECT_EQ(nal.typeName(), "NON_IDR_SLICE");
    nal.nalType = 5;
    EXPECT_EQ(nal.typeName(), "IDR_SLICE");
    nal.nalType = 6;
    EXPECT_EQ(nal.typeName(), "SEI");
    nal.nalType = 7;
    EXPECT_EQ(nal.typeName(), "SPS");
    nal.nalType = 8;
    EXPECT_EQ(nal.typeName(), "PPS");

    EXPECT_NE(nal.summary().find("H.264"), std::string::npos);
    EXPECT_NE(nal.summary().find("offset=0x"), std::string::npos);
}

TEST(NALUnitTest, NamesH265ParameterSets)
{
    NALUnit nal;
    nal.codec = CodecType::H265;

    nal.nalType = 32;
    EXPECT_EQ(nal.typeName(), "VPS");
    nal.nalType = 33;
    EXPECT_EQ(nal.typeName(), "SPS");
    nal.nalType = 34;
    EXPECT_EQ(nal.typeName(), "PPS");
    nal.nalType = 39;
    EXPECT_EQ(nal.typeName(), "PREFIX_SEI");
    nal.nalType = 40;
    EXPECT_EQ(nal.typeName(), "SUFFIX_SEI");
}

TEST(NALUnitTest, NamesHevcTypesBySpec)
{
    NALUnit nal;
    nal.codec = CodecType::H265;

    struct Case
    {
        uint8_t type;
        const char* name;
    };
    const Case cases[] = {
        {2, "TSA_N"},           {3, "TSA_R"},       {4, "STSA_N"},       {5, "STSA_R"},    {6, "RADL_N"},
        {7, "RADL_R"},          {8, "RASL_N"},      {9, "RASL_R"},       {16, "BLA_W_LP"}, {17, "BLA_W_RADL"},
        {18, "BLA_N_LP"},       {19, "IDR_W_RADL"}, {20, "IDR_N_LP"},    {21, "CRA_NUT"},  {22, "RSV_IRAP_VCL22"},
        {31, "RSV_IRAP_VCL31"}, {35, "AUD"},        {36, "EOS_NUT"},     {37, "EOB_NUT"},  {38, "FD_NUT"},
        {41, "RSV_NVCL41"},     {47, "RSV_NVCL47"}, {48, "UNSPECIFIED"},
    };
    for (const Case& c : cases)
    {
        nal.nalType = c.type;
        EXPECT_EQ(nal.typeName(), c.name) << "HEVC NAL type " << static_cast<int>(c.type);
    }
}

TEST(BitstreamParserTest, SplitsH265Buffer)
{
    const uint8_t buffer[] = {
        0x00, 0x00, 0x01, 0x40, 0x01, // VPS
        0x00, 0x00, 0x00, 0x01, 0x42, 0x01, // SPS
    };
    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseBuffer(buffer, sizeof(buffer)));

    const auto& nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 2u);
    EXPECT_EQ(nals[0].nalType, 32);
    EXPECT_EQ(nals[0].startCodeLen, 3);
    EXPECT_EQ(nals[1].nalType, 33);
    EXPECT_EQ(nals[1].startCodeLen, 4);
}


TEST(BitstreamParserTest, DetectsCodecFromAnnexBHeaders)
{
    const uint8_t h264Sps[] = {0x00, 0x00, 0x01, 0x67, 0x64, 0x00};
    const uint8_t h264Idr[] = {0x00, 0x00, 0x01, 0x65, 0x88};
    const uint8_t hevcVps[] = {0x00, 0x00, 0x01, 0x40, 0x01};
    const uint8_t hevcSps[] = {0x00, 0x00, 0x01, 0x42, 0x01};
    const uint8_t hevcVps4[] = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01};
    const uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};

    EXPECT_EQ(BitstreamParser::detectCodec(h264Sps, sizeof(h264Sps)), CodecType::H264);
    EXPECT_EQ(BitstreamParser::detectCodec(h264Idr, sizeof(h264Idr)), CodecType::H264);
    EXPECT_EQ(BitstreamParser::detectCodec(hevcVps, sizeof(hevcVps)), CodecType::H265);
    EXPECT_EQ(BitstreamParser::detectCodec(hevcSps, sizeof(hevcSps)), CodecType::H265);
    EXPECT_EQ(BitstreamParser::detectCodec(hevcVps4, sizeof(hevcVps4)), CodecType::H265);
    EXPECT_EQ(BitstreamParser::detectCodec(junk, sizeof(junk)), CodecType::H264);
}

TEST(BitstreamParserTest, AutoDetectsHevcStream)
{
    const std::string path = std::string(TEST_DATA_DIR) + "/output.h265";
    if (!std::ifstream(path).good())
        GTEST_SKIP() << "sample stream missing";

    // Constructing as H.264 asks the parser to run codec detection.
    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseFile(path)) << parser.lastError();

    const auto& nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 216u);
    EXPECT_EQ(nals.front().codec, CodecType::H265);
    EXPECT_EQ(nals.front().nalType, 32); // VPS
}
