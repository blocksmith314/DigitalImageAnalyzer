#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "bitstreamparser.h"
#include "nalunit.h"

namespace {
const uint8_t k_annex_b[] = {
    0x00, 0x00, 0x01, 0x09, 0xF0,                    // AUD, 3-byte start code
    0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x00, 0x80,  // SEI, 4-byte start code
    0x00, 0x00, 0x01, 0x65, 0x88,                    // IDR slice
};
}

TEST(BitstreamParserTest, SplitsAnnexBBuffer) {
  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseBuffer(k_annex_b, sizeof(k_annex_b)));

  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 3u);
  EXPECT_EQ(nals[0].nal_type, 9);
  EXPECT_EQ(nals[0].start_code_len, 3);
  EXPECT_EQ(nals[0].size, 5u);
  EXPECT_EQ(nals[1].nal_type, 6);
  EXPECT_EQ(nals[1].start_code_len, 4);
  EXPECT_EQ(nals[2].nal_type, 5);
  EXPECT_EQ(nals[2].start_code_len, 3);

  EXPECT_EQ(nals[0].offset, 0u);
  EXPECT_EQ(nals[1].offset, 5u);
  EXPECT_EQ(nals[2].offset, 13u);
  EXPECT_NE(parser.NalData(nals[0]), nullptr);
  EXPECT_NE(parser.NalData(nals[2]), nullptr);
}

TEST(BitstreamParserTest, RejectsBufferWithoutStartCode) {
  const uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};
  BitstreamParser parser(CodecType::kH264);
  EXPECT_FALSE(parser.ParseBuffer(junk, sizeof(junk)));
  EXPECT_NE(parser.LastError().find("No start code"), std::string::npos);
}

TEST(BitstreamParserTest, RejectsMissingFile) {
  BitstreamParser parser(CodecType::kH264);
  EXPECT_FALSE(parser.ParseFile("definitely-missing-stream.h264"));
  EXPECT_FALSE(parser.LastError().empty());
}

TEST(NALUnitTest, NamesH264Types) {
  NALUnit nal;
  nal.codec = CodecType::kH264;
  nal.size = 10;
  nal.offset = 0x2A;

  nal.nal_type = 1;
  EXPECT_EQ(nal.TypeName(), "NON_IDR_SLICE");
  nal.nal_type = 5;
  EXPECT_EQ(nal.TypeName(), "IDR_SLICE");
  nal.nal_type = 6;
  EXPECT_EQ(nal.TypeName(), "SEI");
  nal.nal_type = 7;
  EXPECT_EQ(nal.TypeName(), "SPS");
  nal.nal_type = 8;
  EXPECT_EQ(nal.TypeName(), "PPS");

  EXPECT_NE(nal.summary().find("H.264"), std::string::npos);
  EXPECT_NE(nal.summary().find("offset=0x"), std::string::npos);
}

TEST(NALUnitTest, NamesH265ParameterSets) {
  NALUnit nal;
  nal.codec = CodecType::kH265;

  nal.nal_type = 32;
  EXPECT_EQ(nal.TypeName(), "VPS");
  nal.nal_type = 33;
  EXPECT_EQ(nal.TypeName(), "SPS");
  nal.nal_type = 34;
  EXPECT_EQ(nal.TypeName(), "PPS");
  nal.nal_type = 39;
  EXPECT_EQ(nal.TypeName(), "PREFIX_SEI");
  nal.nal_type = 40;
  EXPECT_EQ(nal.TypeName(), "SUFFIX_SEI");
}

TEST(NALUnitTest, NamesHevcTypesBySpec) {
  NALUnit nal;
  nal.codec = CodecType::kH265;

  struct Case {
    uint8_t type;
    const char* name;
  };
  const Case cases[] = {
      {2, "TSA_N"},           {3, "TSA_R"},        {4, "STSA_N"},
      {5, "STSA_R"},          {6, "RADL_N"},       {7, "RADL_R"},
      {8, "RASL_N"},          {9, "RASL_R"},       {16, "BLA_W_LP"},
      {17, "BLA_W_RADL"},     {18, "BLA_N_LP"},    {19, "IDR_W_RADL"},
      {20, "IDR_N_LP"},       {21, "CRA_NUT"},     {22, "RSV_IRAP_VCL22"},
      {31, "RSV_IRAP_VCL31"}, {35, "AUD"},         {36, "EOS_NUT"},
      {37, "EOB_NUT"},        {38, "FD_NUT"},      {41, "RSV_NVCL41"},
      {47, "RSV_NVCL47"},     {48, "UNSPECIFIED"},
  };
  for (const Case& c : cases) {
    nal.nal_type = c.type;
    EXPECT_EQ(nal.TypeName(), c.name)
        << "HEVC NAL type " << static_cast<int>(c.type);
  }
}

TEST(BitstreamParserTest, SplitsH265Buffer) {
  const uint8_t buffer[] = {
      0x00, 0x00, 0x01, 0x40, 0x01,        // VPS
      0x00, 0x00, 0x00, 0x01, 0x42, 0x01,  // SPS
  };
  BitstreamParser parser(CodecType::kH265);
  ASSERT_TRUE(parser.ParseBuffer(buffer, sizeof(buffer)));

  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 2u);
  EXPECT_EQ(nals[0].nal_type, 32);
  EXPECT_EQ(nals[0].start_code_len, 3);
  EXPECT_EQ(nals[1].nal_type, 33);
  EXPECT_EQ(nals[1].start_code_len, 4);
}

TEST(BitstreamParserTest, DetectsCodecFromAnnexBHeaders) {
  const uint8_t h264_sps[] = {0x00, 0x00, 0x01, 0x67, 0x64, 0x00};
  const uint8_t h264_idr[] = {0x00, 0x00, 0x01, 0x65, 0x88};
  const uint8_t hevc_vps[] = {0x00, 0x00, 0x01, 0x40, 0x01};
  const uint8_t hevc_sps[] = {0x00, 0x00, 0x01, 0x42, 0x01};
  const uint8_t hevc_vps4[] = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01};
  const uint8_t junk[] = {'h', 'e', 'l', 'l', 'o'};

  EXPECT_EQ(BitstreamParser::DetectCodec(h264_sps, sizeof(h264_sps)),
            CodecType::kH264);
  EXPECT_EQ(BitstreamParser::DetectCodec(h264_idr, sizeof(h264_idr)),
            CodecType::kH264);
  EXPECT_EQ(BitstreamParser::DetectCodec(hevc_vps, sizeof(hevc_vps)),
            CodecType::kH265);
  EXPECT_EQ(BitstreamParser::DetectCodec(hevc_sps, sizeof(hevc_sps)),
            CodecType::kH265);
  EXPECT_EQ(BitstreamParser::DetectCodec(hevc_vps4, sizeof(hevc_vps4)),
            CodecType::kH265);
  EXPECT_EQ(BitstreamParser::DetectCodec(junk, sizeof(junk)), CodecType::kH264);
}

TEST(BitstreamParserTest, AutoDetectsHevcStream) {
  const std::string path = std::string(TEST_DATA_DIR) + "/output.h265";
  if (!std::ifstream(path).good()) GTEST_SKIP() << "sample stream missing";

  // Constructing as H.264 asks the parser to run codec detection.
  BitstreamParser parser(CodecType::kH264);
  ASSERT_TRUE(parser.ParseFile(path)) << parser.LastError();

  const auto& nals = parser.NalUnits();
  ASSERT_EQ(nals.size(), 216u);
  EXPECT_EQ(nals.front().codec, CodecType::kH265);
  EXPECT_EQ(nals.front().nal_type, 32);  // VPS
}
