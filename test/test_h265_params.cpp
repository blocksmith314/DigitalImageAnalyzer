#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "bitstreamparser.h"
#include "h265types.h"

namespace {

std::string sample_path() {
  return std::string(TEST_DATA_DIR) + "/output.h265";
}
bool sample_exists() {
  std::ifstream file(sample_path());
  return file.good();
}

const NALUnit* firstOfType(const std::vector<NALUnit>& nals, uint8_t type) {
  for (const auto& nal : nals) {
    if (nal.nal_type == type) return &nal;
  }
  return nullptr;
}

}  // namespace

TEST(H265ParamsTest, ParsesVps) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH265);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* nal = firstOfType(parser.NalUnits(), 32);
  ASSERT_NE(nal, nullptr);
  H265VPS vps;
  ASSERT_TRUE(ParseH265Vps(*nal, parser.NalData(*nal), vps));

  EXPECT_EQ(vps.vps_id, 0u);
  EXPECT_EQ(vps.max_sub_layers_minus1, 0u);
  EXPECT_TRUE(vps.temporal_id_nesting_flag);
}

TEST(H265ParamsTest, ParsesPps) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH265);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* nal = firstOfType(parser.NalUnits(), 34);
  ASSERT_NE(nal, nullptr);
  H265PPS pps;
  ASSERT_TRUE(ParseH265Pps(*nal, parser.NalData(*nal), pps));

  EXPECT_EQ(pps.pps_id, 0u);
  EXPECT_EQ(pps.sps_id, 0u);
}

TEST(H265ParamsTest, NamesIrapSlices) {
  if (!sample_exists()) GTEST_SKIP() << "sample stream missing";

  BitstreamParser parser(CodecType::kH265);
  ASSERT_TRUE(parser.ParseFile(sample_path())) << parser.LastError();

  const NALUnit* idr = firstOfType(parser.NalUnits(), 20);
  const NALUnit* cra = firstOfType(parser.NalUnits(), 21);
  ASSERT_NE(idr, nullptr);
  ASSERT_NE(cra, nullptr);
  EXPECT_EQ(idr->TypeName(), "IDR_N_LP");
  EXPECT_EQ(cra->TypeName(), "CRA_NUT");
}
