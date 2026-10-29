#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "seiparser.h"

TEST(SeiTest, ParsesUserDataUnregistered) {
  // start code + NAL header + payload type 5 + size 21 + UUID(16) + "hello" +
  // rbsp stop bit
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x15};
  for (int i = 0; i < 16; i++) data.push_back(static_cast<uint8_t>(i));
  const char text[] = "hello";
  data.insert(data.end(), text, text + 5);
  data.push_back(0x80);

  NALUnit nal;
  nal.codec = CodecType::kH264;
  nal.start_code_len = 4;
  nal.nal_type = 6;
  nal.size = static_cast<uint32_t>(data.size());

  std::vector<SeiMessage> messages;
  ASSERT_TRUE(ParseSeiMessages(nal, data.data(), false, messages));
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0].type, 5u);
  EXPECT_EQ(messages[0].size, 21u);
  EXPECT_NE(messages[0].text.find("hello"), std::string::npos);
}

TEST(SeiTest, NamesKnownPayloadTypes) {
  EXPECT_EQ(SeiTypeName(5), "user_data_unregistered");
}

TEST(SeiTest, ParsesRecoveryPoint) {
  // start code + NAL header + payload type 6 + size 1 + payload + rbsp stop
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x06,
                               0x06, 0x01, 0x80, 0x80};

  NALUnit nal;
  nal.codec = CodecType::kH264;
  nal.start_code_len = 4;
  nal.nal_type = 6;
  nal.size = static_cast<uint32_t>(data.size());

  std::vector<SeiMessage> messages;
  ASSERT_TRUE(ParseSeiMessages(nal, data.data(), false, messages));
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0].type, 6u);
  EXPECT_NE(messages[0].text.find("recovery_frame_cnt"), std::string::npos);
}

TEST(SeiTest, ParsesMultipleMessages) {
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x06};
  // user_data_unregistered: type 5, size 21, UUID + "hello"
  data.push_back(0x05);
  data.push_back(0x15);
  for (int i = 0; i < 16; i++) data.push_back(static_cast<uint8_t>(i));
  const char text[] = "hello";
  data.insert(data.end(), text, text + 5);
  // recovery_point: type 6, size 1
  data.push_back(0x06);
  data.push_back(0x01);
  data.push_back(0x80);
  data.push_back(0x80);  // rbsp stop bit

  NALUnit nal;
  nal.codec = CodecType::kH264;
  nal.start_code_len = 4;
  nal.nal_type = 6;
  nal.size = static_cast<uint32_t>(data.size());

  std::vector<SeiMessage> messages;
  ASSERT_TRUE(ParseSeiMessages(nal, data.data(), false, messages));
  ASSERT_EQ(messages.size(), 2u);
  EXPECT_EQ(messages[0].type, 5u);
  EXPECT_EQ(messages[1].type, 6u);
}
