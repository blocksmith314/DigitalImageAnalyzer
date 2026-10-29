#ifndef DIA_CORE_EXPGOLOMB_H_
#define DIA_CORE_EXPGOLOMB_H_

#include <cstdint>
#include <vector>

// Streaming RBSP reader with zero copies: skips emulation-prevention bytes
// (0x000003) on the fly while decoding. Suitable for slice headers where only
// the first few bytes are needed and copying the whole NAL is too expensive.
class RbspBitReader {
 public:
  RbspBitReader(const uint8_t* data, size_t len) : Data(data), Len(len) {}

  bool ReadBit();
  uint64_t ReadBits(int n);
  uint64_t ReadUe();
  bool eof() const { return eof_ && bits_left_ == 0; }

 private:
  uint8_t NextByte();

  const uint8_t* Data;
  size_t Len;
  size_t pos_ = 0;
  uint8_t cur_ = 0;
  int bits_left_ = 0;
  uint8_t prev1_ = 0;
  uint8_t prev2_ = 0;
  bool eof_ = false;
};

class BitReader {
 public:
  explicit BitReader(const std::vector<uint8_t>& data);
  BitReader(const uint8_t* data, size_t len);

  uint64_t ReadBits(int n);
  uint64_t ReadUe();
  int64_t ReadSe();
  bool ReadBit();
  bool eof() const;

  size_t BitsRead() const { return pos_ * 8 + bit_pos_; }
  size_t RemainingBits() const { return Len * 8 - BitsRead(); }
  // True when data remains before RbspTrailingBits().
  bool MoreRbspData() const;

 private:
  std::vector<uint8_t> owned_;
  const uint8_t* Data;
  size_t Len;
  size_t pos_ = 0;
  int bit_pos_ = 0;
};

#endif  // DIA_CORE_EXPGOLOMB_H_
