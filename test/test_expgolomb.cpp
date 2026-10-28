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

namespace {

// Small helper to build bit sequences for the exp-golomb tests.
struct BitWriter {
    std::vector<uint8_t> bytes;
    int bits = 0;

    void put(bool bit) {
        if (bits % 8 == 0) bytes.push_back(0);
        if (bit) bytes.back() |= static_cast<uint8_t>(1u << (7 - bits % 8));
        bits++;
    }
    void putBits(uint64_t value, int count) {
        for (int i = count - 1; i >= 0; i--) put(((value >> i) & 1u) != 0);
    }
    void putUe(uint64_t value) {
        const uint64_t v = value + 1;
        int length = 0;
        for (uint64_t t = v; t; t >>= 1) length++;
        for (int i = 0; i < length - 1; i++) put(false);
        putBits(v, length);
    }
};

} // namespace

TEST(BitReaderTest, DecodesLongExpGolombValues) {
    const uint64_t values[] = {0, 1, 2, 3, 7, 8, 1000, 65535};
    BitWriter writer;
    for (uint64_t v : values) writer.putUe(v);

    BitReader reader(writer.bytes.data(), writer.bytes.size());
    for (uint64_t v : values) EXPECT_EQ(reader.readUe(), v);
}

TEST(BitReaderTest, DecodesSignedExpGolombRange) {
    const int64_t values[] = {-100, -3, -1, 0, 1, 3, 100};
    BitWriter writer;
    for (int64_t v : values) {
        const uint64_t ue = (v > 0) ? static_cast<uint64_t>(2 * v - 1)
                                    : static_cast<uint64_t>(-2 * v);
        writer.putUe(ue);
    }

    BitReader reader(writer.bytes.data(), writer.bytes.size());
    for (int64_t v : values) EXPECT_EQ(reader.readSe(), v);
}

TEST(BitReaderTest, DetectsRbspTrailingBits) {
    const uint8_t stopOnly[] = {0x80};  // 1 followed by zero padding
    BitReader onlyStop(stopOnly, sizeof(stopOnly));
    EXPECT_FALSE(onlyStop.moreRbspData());

    const uint8_t withData[] = {0xA8};  // 1010 1000: data before the stop bit
    BitReader dataFirst(withData, sizeof(withData));
    EXPECT_TRUE(dataFirst.moreRbspData());
}
