#pragma once

#include <cstdint>
#include <vector>

// Streaming RBSP reader with zero copies: skips emulation-prevention bytes
// (0x000003) on the fly while decoding. Suitable for slice headers where only
// the first few bytes are needed and copying the whole NAL is too expensive.
class RbspBitReader {
public:
    RbspBitReader(const uint8_t *data, size_t len) : m_data(data), m_len(len) {}

    bool readBit();
    uint64_t readBits(int n);
    uint64_t readUe();
    bool eof() const { return m_eof && m_bitsLeft == 0; }

private:
    uint8_t nextByte();

    const uint8_t *m_data;
    size_t m_len;
    size_t m_pos = 0;
    uint8_t m_cur = 0;
    int m_bitsLeft = 0;
    uint8_t m_prev1 = 0;
    uint8_t m_prev2 = 0;
    bool m_eof = false;
};

class BitReader {
public:
    explicit BitReader(const std::vector<uint8_t> &data);
    BitReader(const uint8_t *data, size_t len);

    uint64_t readBits(int n);
    uint64_t readUe();
    int64_t readSe();
    bool readBit();
    bool eof() const;

    size_t bitsRead() const { return m_pos * 8 + m_bitPos; }
    size_t remainingBits() const { return m_len * 8 - bitsRead(); }
    // True when data remains before rbsp_trailing_bits().
    bool moreRbspData() const;

private:
    std::vector<uint8_t> m_owned;
    const uint8_t *m_data;
    size_t m_len;
    size_t m_pos = 0;
    int m_bitPos = 0;
};