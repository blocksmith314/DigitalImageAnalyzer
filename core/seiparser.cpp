#include "seiparser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

std::string stripEmulationPrevention(const uint8_t *data, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; i++) {
        if (i + 2 < len && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 3) {
            out.push_back((char)0);
            out.push_back((char)0);
            i += 2;
        } else {
            out.push_back((char)data[i]);
        }
    }
    return out;
}

std::string printable(const uint8_t *data, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; i++) {
        const uint8_t c = data[i];
        out.push_back((c >= 0x20 && c <= 0x7E) ? (char)c : '.');
    }
    return out;
}

std::string hexPreview(const uint8_t *data, size_t len, size_t maxBytes = 16) {
    std::string out;
    const size_t n = std::min(len, maxBytes);
    char buf[4];
    for (size_t i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf), "%02x", data[i]);
        out += buf;
    }
    if (len > n) out += "...";
    return out;
}

std::string uuidString(const uint8_t *p) {
    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                  p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    return buf;
}

// Bit reader for a byte-aligned SEI payload (no emulation prevention here).
class PayloadReader {
public:
    PayloadReader(const uint8_t *data, size_t len) : m_data(data), m_len(len) {}

    bool eof() const { return m_byte >= m_len; }

    uint32_t bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v = (v << 1) | nextBit();
        return v;
    }

    uint32_t ue() {
        int zeros = 0;
        while (!eof() && nextBit() == 0 && zeros < 31) zeros++;
        if (zeros >= 31) return 0;
        return ((1u << zeros) - 1u) + (zeros ? bits(zeros) : 0u);
    }

    size_t bytePos() const { return m_byte; }

private:
    uint32_t nextBit() {
        if (m_byte >= m_len) return 0;
        const uint32_t b = (m_data[m_byte] >> (7 - m_bit)) & 1;
        if (++m_bit == 8) {
            m_bit = 0;
            m_byte++;
        }
        return b;
    }

    const uint8_t *m_data;
    size_t m_len;
    size_t m_byte = 0;
    int m_bit = 0;
};

std::string decodeMessage(uint32_t type, const uint8_t *p, uint32_t size) {
    char buf[256];
    switch (type) {
        case 5: {   // user_data_unregistered
            if (size >= 16) {
                std::string text = printable(p + 16, size - 16);
                return "UUID " + uuidString(p) + "\n    text: \"" + text + "\"";
            }
            return "(too short) " + hexPreview(p, size);
        }
        case 4: {   // user_data_registered_itu_t_t35
            PayloadReader r(p, size);
            const uint32_t country = r.bits(8);
            char cbuf[8];
            std::snprintf(cbuf, sizeof(cbuf), "%02X", country);
            std::string s = std::string("country_code=0x") + cbuf;
            if (country == 0xFF && !r.eof()) {
                const uint32_t ext = r.bits(8);
                std::snprintf(cbuf, sizeof(cbuf), "%02X", ext);
                s += " extension=0x" + std::string(cbuf);
            }
            const size_t pos = std::min(r.bytePos(), (size_t)size);
            std::string text = printable(p + pos, size - pos);
            s += "\n    data: \"" + text + "\"";
            return s;
        }
        case 6: {   // recovery_point
            PayloadReader r(p, size);
            const uint32_t cnt = r.ue();
            const uint32_t exact = r.bits(1);
            const uint32_t broken = r.bits(1);
            const uint32_t changing = r.bits(2);
            std::snprintf(buf, sizeof(buf),
                          "recovery_frame_cnt=%u exact_match=%u broken_link=%u changing_slice_group=%u",
                          cnt, exact, broken, changing);
            return buf;
        }
        case 137: { // mastering_display_colour_volume
            if (size < 24) return "(too short) " + hexPreview(p, size);
            PayloadReader r(p, size);
            uint32_t px[3], py[3];
            for (int i = 0; i < 3; i++) {
                px[i] = r.bits(16);
                py[i] = r.bits(16);
            }
            const uint32_t wx = r.bits(16), wy = r.bits(16);
            const uint32_t maxL = r.bits(32), minL = r.bits(32);
            std::snprintf(buf, sizeof(buf),
                          "R(%u,%u) G(%u,%u) B(%u,%u) white(%u,%u) max_luminance=%u min_luminance=%u",
                          px[0], py[0], px[1], py[1], px[2], py[2], wx, wy, maxL, minL);
            return buf;
        }
        case 144: { // content_light_level_info
            if (size < 4) return "(too short) " + hexPreview(p, size);
            PayloadReader r(p, size);
            const uint32_t maxCll = r.bits(16);
            const uint32_t maxFall = r.bits(16);
            std::snprintf(buf, sizeof(buf),
                          "max_content_light_level=%u max_pic_average_light_level=%u", maxCll, maxFall);
            return buf;
        }
        case 147: { // alternative_transfer_characteristics
            if (size < 1) return "(empty)";
            std::snprintf(buf, sizeof(buf), "preferred_transfer_characteristics=%u", p[0]);
            return buf;
        }
        case 0:
            return "buffering period (payload not decoded)";
        case 1:
            return "pic timing (payload not decoded)";
        case 3:
            return "filler payload";
        case 45:
            return "frame packing arrangement (payload not decoded)";
        default:
            break;
    }
    return "size=" + std::to_string(size) + " data=" + hexPreview(p, size);
}

} // namespace

std::string seiTypeName(uint32_t type) {
    switch (type) {
        case 0: return "buffering_period";
        case 1: return "pic_timing";
        case 2: return "pan_scan_rect";
        case 3: return "filler_payload";
        case 4: return "user_data_registered_itu_t_t35";
        case 5: return "user_data_unregistered";
        case 6: return "recovery_point";
        case 7: return "scene_info";
        case 45: return "frame_packing_arrangement";
        case 47: return "display_orientation";
        case 137: return "mastering_display_colour_volume";
        case 144: return "content_light_level_info";
        case 147: return "alternative_transfer_characteristics";
        default: return "unknown";
    }
}

bool parseSeiMessages(const NALUnit &nal, const uint8_t *data, bool h265,
                      std::vector<SeiMessage> &out) {
    const size_t headerLen = h265 ? 2 : 1;
    const size_t start = nal.startCodeLen + headerLen;
    if (!data || start >= nal.size) return false;

    const std::string rbsp = stripEmulationPrevention(data + start, nal.size - start);
    const uint8_t *payloadData = reinterpret_cast<const uint8_t *>(rbsp.data());
    const size_t len = rbsp.size();

    size_t i = 0;
    bool any = false;
    while (i < len) {
        if (payloadData[i] == 0x80) break; // rbsp_trailing_bits

        uint32_t type = 0;
        while (i < len && payloadData[i] == 0xFF) {
            type += 255;
            i++;
        }
        if (i >= len) break;
        type += payloadData[i++];

        uint32_t size = 0;
        while (i < len && payloadData[i] == 0xFF) {
            size += 255;
            i++;
        }
        if (i >= len) break;
        size += payloadData[i++];

        if (size > len - i) size = (uint32_t)(len - i);

        SeiMessage msg;
        msg.type = type;
        msg.size = size;
        msg.text = decodeMessage(type, payloadData + i, size);
        out.push_back(msg);
        any = true;

        i += size;
    }
    return any;
}
