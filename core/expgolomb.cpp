#include "expgolomb.h"

#include <stdexcept>

static std::vector<uint8_t> remove_emulation_prevention(const uint8_t* data,
                                                        size_t len) {
  std::vector<uint8_t> out;
  out.reserve(len);
  size_t i = 0;
  while (i < len) {
    if (i + 2 < len && data[i] == 0x00 && data[i + 1] == 0x00 &&
        data[i + 2] == 0x03) {
      out.push_back(0x00);
      out.push_back(0x00);
      i += 3;
    } else {
      out.push_back(data[i]);
      i++;
    }
  }
  return out;
}

BitReader::BitReader(const std::vector<uint8_t>& data)
    : owned_(remove_emulation_prevention(data.data(), data.size())),
      Data(owned_.data()),
      Len(owned_.size()) {}

BitReader::BitReader(const uint8_t* data, size_t len)
    : owned_(remove_emulation_prevention(data, len)),
      Data(owned_.data()),
      Len(owned_.size()) {}

uint8_t RbspBitReader::NextByte() {
  while (pos_ < Len) {
    const uint8_t b = Data[pos_++];
    if (b == 0x03 && prev1_ == 0x00 && prev2_ == 0x00) continue;  // EPB
    prev2_ = prev1_;
    prev1_ = b;
    return b;
  }
  eof_ = true;
  return 0;
}

bool RbspBitReader::ReadBit() {
  if (bits_left_ == 0) {
    if (eof_) return false;
    cur_ = NextByte();
    if (eof_) return false;
    bits_left_ = 8;
  }
  const bool bit = (cur_ & 0x80) != 0;
  cur_ = (uint8_t)(cur_ << 1);
  bits_left_--;
  return bit;
}

uint64_t RbspBitReader::ReadBits(int n) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v = (v << 1) | (ReadBit() ? 1 : 0);
  return v;
}

uint64_t RbspBitReader::ReadUe() {
  int zeros = 0;
  while (!eof() && !ReadBit() && zeros < 32) zeros++;
  if (zeros >= 32) return 0;
  return ((1ULL << zeros) - 1) + (zeros ? ReadBits(zeros) : 0);
}

bool BitReader::eof() const { return pos_ >= Len; }

bool BitReader::MoreRbspData() const {
  const size_t rem = RemainingBits();
  if (rem == 0) return false;
  const size_t base = BitsRead();
  for (size_t i = rem; i-- > 0;) {
    const size_t abs_bit = base + i;
    const uint8_t byte = Data[abs_bit / 8];
    if ((byte >> (7 - (int)(abs_bit % 8))) & 1)
      return i > 0;  // rbsp_stop_one_bit found; data before it?
  }
  return false;
}

bool BitReader::ReadBit() {
  if (pos_ >= Len) return false;
  uint8_t bit = (Data[pos_] >> (7 - bit_pos_)) & 1;
  bit_pos_++;
  if (bit_pos_ == 8) {
    bit_pos_ = 0;
    pos_++;
  }
  return bit != 0;
}

uint64_t BitReader::ReadBits(int n) {
  uint64_t val = 0;
  for (int i = 0; i < n; i++) {
    val = (val << 1) | (ReadBit() ? 1 : 0);
  }
  return val;
}

uint64_t BitReader::ReadUe() {
  int leading_zeros = -1;
  bool bit = false;
  do {
    bit = ReadBit();
    leading_zeros++;
  } while (!bit && !eof());

  if (leading_zeros == 0) return 0;

  uint64_t val = ReadBits(leading_zeros);
  return (1ULL << leading_zeros) - 1 + val;
}

int64_t BitReader::ReadSe() {
  uint64_t ue = ReadUe();
  int64_t n = (int64_t)ue;
  if (n % 2 == 0) {
    return -(n / 2);
  } else {
    return (n + 1) / 2;
  }
}
