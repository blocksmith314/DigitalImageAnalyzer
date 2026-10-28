#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "nalunit.h"

#include <string>
#include <vector>

namespace {
const uint8_t kAnnexB[] = {
    0x00, 0x00, 0x01, 0x09, 0xF0,                          // AUD, 3-byte start code
    0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x00, 0x80,        // SEI, 4-byte start code
    0x00, 0x00, 0x01, 0x65, 0x88,                          // IDR slice
};
}

TEST(BitstreamParserTest, SplitsAnnexBBuffer) {
    BitstreamParser parser(CodecType::H264);
    ASSERT_TRUE(parser.parseBuffer(kAnnexB, sizeof(kAnnexB)));

    const auto &nals = parser.nalUnits();
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

TEST(BitstreamParserTest, RejectsBufferWithoutStartCode) {
    const uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};
    BitstreamParser parser(CodecType::H264);
    EXPECT_FALSE(parser.parseBuffer(junk, sizeof(junk)));
    EXPECT_NE(parser.lastError().find("No start code"), std::string::npos);
}

TEST(BitstreamParserTest, RejectsMissingFile) {
    BitstreamParser parser(CodecType::H264);
    EXPECT_FALSE(parser.parseFile("definitely-missing-stream.h264"));
    EXPECT_FALSE(parser.lastError().empty());
}

TEST(NALUnitTest, NamesH264Types) {
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

TEST(NALUnitTest, NamesH265ParameterSets) {
    NALUnit nal;
    nal.codec = CodecType::H265;

    nal.nalType = 32;
    EXPECT_EQ(nal.typeName(), "VPS");
    nal.nalType = 33;
    EXPECT_EQ(nal.typeName(), "SPS");
    nal.nalType = 34;
    EXPECT_EQ(nal.typeName(), "PPS");
    nal.nalType = 39;
    EXPECT_EQ(nal.typeName(), "SUFFIX_SEI");
}

TEST(BitstreamParserTest, SplitsH265Buffer) {
    const uint8_t buffer[] = {
        0x00, 0x00, 0x01, 0x40, 0x01,        // VPS
        0x00, 0x00, 0x00, 0x01, 0x42, 0x01,  // SPS
    };
    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseBuffer(buffer, sizeof(buffer)));

    const auto &nals = parser.nalUnits();
    ASSERT_EQ(nals.size(), 2u);
    EXPECT_EQ(nals[0].nalType, 32);
    EXPECT_EQ(nals[0].startCodeLen, 3);
    EXPECT_EQ(nals[1].nalType, 33);
    EXPECT_EQ(nals[1].startCodeLen, 4);
}
