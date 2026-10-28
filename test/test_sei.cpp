#include <gtest/gtest.h>

#include "seiparser.h"

#include <string>
#include <vector>

TEST(SeiTest, ParsesUserDataUnregistered) {
    // start code + NAL header + payload type 5 + size 21 + UUID(16) + "hello" + rbsp stop bit
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x15};
    for (int i = 0; i < 16; i++) data.push_back(static_cast<uint8_t>(i));
    const char text[] = "hello";
    data.insert(data.end(), text, text + 5);
    data.push_back(0x80);

    NALUnit nal;
    nal.codec = CodecType::H264;
    nal.startCodeLen = 4;
    nal.nalType = 6;
    nal.size = static_cast<uint32_t>(data.size());

    std::vector<SeiMessage> messages;
    ASSERT_TRUE(parseSeiMessages(nal, data.data(), false, messages));
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].type, 5u);
    EXPECT_EQ(messages[0].size, 21u);
    EXPECT_NE(messages[0].text.find("hello"), std::string::npos);
}

TEST(SeiTest, NamesKnownPayloadTypes) {
    EXPECT_EQ(seiTypeName(5), "user_data_unregistered");
}
