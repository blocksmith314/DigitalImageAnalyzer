#pragma once

#include <cstdint>
#include <vector>

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