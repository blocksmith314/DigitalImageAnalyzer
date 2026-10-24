#include "expgolomb.h"
#include <stdexcept>

static std::vector<uint8_t> removeEmulationPrevention(const uint8_t *data, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len);
    size_t i = 0;
    while (i < len) {
        if (i + 2 < len && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x03) {
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

BitReader::BitReader(const std::vector<uint8_t> &data)
    : m_owned(removeEmulationPrevention(data.data(), data.size())),
      m_data(m_owned.data()), m_len(m_owned.size()) {}

BitReader::BitReader(const uint8_t *data, size_t len)
    : m_owned(removeEmulationPrevention(data, len)),
      m_data(m_owned.data()), m_len(m_owned.size()) {}

uint8_t RbspBitReader::nextByte() {
    while (m_pos < m_len) {
        const uint8_t b = m_data[m_pos++];
        if (b == 0x03 && m_prev1 == 0x00 && m_prev2 == 0x00) continue; // EPB
        m_prev2 = m_prev1;
        m_prev1 = b;
        return b;
    }
    m_eof = true;
    return 0;
}

bool RbspBitReader::readBit() {
    if (m_bitsLeft == 0) {
        if (m_eof) return false;
        m_cur = nextByte();
        if (m_eof) return false;
        m_bitsLeft = 8;
    }
    const bool bit = (m_cur & 0x80) != 0;
    m_cur = (uint8_t)(m_cur << 1);
    m_bitsLeft--;
    return bit;
}

uint64_t RbspBitReader::readBits(int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (readBit() ? 1 : 0);
    return v;
}

uint64_t RbspBitReader::readUe() {
    int zeros = 0;
    while (!eof() && !readBit() && zeros < 32) zeros++;
    if (zeros >= 32) return 0;
    return ((1ULL << zeros) - 1) + (zeros ? readBits(zeros) : 0);
}

bool BitReader::eof() const {
    return m_pos >= m_len;
}

bool BitReader::moreRbspData() const {
    const size_t rem = remainingBits();
    if (rem == 0) return false;
    const size_t base = bitsRead();
    for (size_t i = rem; i-- > 0;) {
        const size_t absBit = base + i;
        const uint8_t byte = m_data[absBit / 8];
        if ((byte >> (7 - (int)(absBit % 8))) & 1)
            return i > 0;   // rbsp_stop_one_bit found; data before it?
    }
    return false;
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