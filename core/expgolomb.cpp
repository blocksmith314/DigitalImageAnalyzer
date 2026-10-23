#include "expgolomb.h"
#include <stdexcept>

BitReader::BitReader(const std::vector<uint8_t> &data)
    : m_data(data.data()), m_len(data.size()) {}

BitReader::BitReader(const uint8_t *data, size_t len)
    : m_data(data), m_len(len) {}

bool BitReader::eof() const {
    return m_pos >= m_len;
}

bool BitReader::readBit() {
    if (m_pos >= m_len) return false;
    uint8_t bit = (m_data[m_pos] >> (7 - m_bitPos)) & 1;
    m_bitPos++;
    if (m_bitPos == 8) {
        m_bitPos = 0;
        m_pos++;
    }
    return bit != 0;
}

uint64_t BitReader::readBits(int n) {
    uint64_t val = 0;
    for (int i = 0; i < n; i++) {
        val = (val << 1) | (readBit() ? 1 : 0);
    }
    return val;
}

uint64_t BitReader::readUe() {
    int leadingZeros = -1;
    bool bit = false;
    do {
        bit = readBit();
        leadingZeros++;
    } while (!bit && !eof());

    if (leadingZeros == 0) return 0;

    uint64_t val = readBits(leadingZeros);
    return (1ULL << leadingZeros) - 1 + val;
}

int64_t BitReader::readSe() {
    uint64_t ue = readUe();
    int64_t n = (int64_t)ue;
    if (n % 2 == 0) {
        return -(n / 2);
    } else {
        return (n + 1) / 2;
    }
}