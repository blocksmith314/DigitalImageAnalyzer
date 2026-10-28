#include <gtest/gtest.h>

#include "expgolomb.h"

TEST(BitReaderTest, ReadsFixedBits) {
    const uint8_t data[] = {0x0A};  // 0000 1010
    BitReader reader(data, sizeof(data));
    EXPECT_EQ(reader.readBits(4), 0u);
    EXPECT_EQ(reader.readBits(4), 10u);
    EXPECT_TRUE(reader.eof());
}

TEST(BitReaderTest, ReadsUnsignedExpGolomb) {
    // ue(0) = 1, ue(1) = 010, ue(2) = 011  ->  1010 0110
    const uint8_t data[] = {0xA6};
    BitReader reader(data, sizeof(data));
    EXPECT_EQ(reader.readUe(), 0u);
    EXPECT_EQ(reader.readUe(), 1u);
    EXPECT_EQ(reader.readUe(), 2u);
}

TEST(BitReaderTest, ReadsSignedExpGolomb) {
    // se(0) = 1, se(1) = 010, se(-1) = 011  ->  1010 0110
    const uint8_t data[] = {0xA6};
    BitReader reader(data, sizeof(data));
    EXPECT_EQ(reader.readSe(), 0);
    EXPECT_EQ(reader.readSe(), 1);
    EXPECT_EQ(reader.readSe(), -1);
}

TEST(RbspBitReaderTest, SkipsEmulationPreventionBytes) {
    const uint8_t data[] = {0x00, 0x00, 0x03, 0x01};  // decodes to 00 00 01
    RbspBitReader reader(data, sizeof(data));
    EXPECT_EQ(reader.readBits(8), 0u);
    EXPECT_EQ(reader.readBits(8), 0u);
    EXPECT_EQ(reader.readBits(8), 1u);
}
