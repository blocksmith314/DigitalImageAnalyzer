#include <gtest/gtest.h>

#include "bitstreamparser.h"
#include "h265types.h"

#include <fstream>
#include <string>

namespace {

std::string samplePath() { return std::string(TEST_DATA_DIR) + "/output.h265"; }
bool sampleExists() { std::ifstream file(samplePath()); return file.good(); }

const NALUnit *firstOfType(const std::vector<NALUnit> &nals, uint8_t type) {
    for (const auto &nal : nals) {
        if (nal.nalType == type) return &nal;
    }
    return nullptr;
}

} // namespace

TEST(H265ParamsTest, ParsesVps) {
    if (!sampleExists()) GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit *nal = firstOfType(parser.nalUnits(), 32);
    ASSERT_NE(nal, nullptr);
    H265VPS vps;
    ASSERT_TRUE(parseH265VPS(*nal, parser.nalData(*nal), vps));

    EXPECT_EQ(vps.vps_id, 0u);
    EXPECT_EQ(vps.max_sub_layers_minus1, 0u);
    EXPECT_TRUE(vps.temporal_id_nesting_flag);
}

TEST(H265ParamsTest, ParsesPps) {
    if (!sampleExists()) GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit *nal = firstOfType(parser.nalUnits(), 34);
    ASSERT_NE(nal, nullptr);
    H265PPS pps;
    ASSERT_TRUE(parseH265PPS(*nal, parser.nalData(*nal), pps));

    EXPECT_EQ(pps.pps_id, 0u);
    EXPECT_EQ(pps.sps_id, 0u);
}


TEST(H265ParamsTest, NamesIrapSlices) {
    if (!sampleExists()) GTEST_SKIP() << "sample stream missing";

    BitstreamParser parser(CodecType::H265);
    ASSERT_TRUE(parser.parseFile(samplePath())) << parser.lastError();

    const NALUnit *idr = firstOfType(parser.nalUnits(), 20);
    const NALUnit *cra = firstOfType(parser.nalUnits(), 21);
    ASSERT_NE(idr, nullptr);
    ASSERT_NE(cra, nullptr);
    EXPECT_EQ(idr->typeName(), "IDR_N_LP");
    EXPECT_EQ(cra->typeName(), "CRA_NUT");
}
