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

private:
    std::vector<uint8_t> m_owned;
    const uint8_t *m_data;
    size_t m_len;
    size_t m_pos = 0;
    int m_bitPos = 0;
};