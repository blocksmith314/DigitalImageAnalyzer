#include "bitstreamparser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

BitstreamParser::BitstreamParser(CodecType codec) : m_codec(codec) {}

BitstreamParser::~BitstreamParser() { resetStorage(); }

BitstreamParser::BitstreamParser(BitstreamParser &&other) noexcept
    : m_codec(other.m_codec),
      m_nals(std::move(other.m_nals)),
      m_error(std::move(other.m_error)),
      m_buffer(std::move(other.m_buffer)),
      m_mapped(std::move(other.m_mapped)),
      m_base(other.m_base) {
    other.m_base = nullptr;
}

BitstreamParser &BitstreamParser::operator=(BitstreamParser &&other) noexcept {
    if (this != &other) {
        resetStorage();
        m_codec = other.m_codec;
        m_nals = std::move(other.m_nals);
        m_error = std::move(other.m_error);
        m_buffer = std::move(other.m_buffer);
        m_mapped = std::move(other.m_mapped);
        m_base = other.m_base;
        other.m_base = nullptr;
    }
    return *this;
}

void BitstreamParser::releaseMappedPages() {
    m_mapped.releaseResidentPages();
}

void BitstreamParser::resetStorage() {
    m_mapped.close();
    m_base = nullptr;
    m_buffer.clear();
    m_extra.clear();
    m_nals.clear();
}

static int64_t findStartCode(const uint8_t *data, size_t len, size_t pos, uint8_t &scLen) {
    for (size_t i = pos; i + 3 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                scLen = 3;
                return (int64_t)i;
            }
            if (i + 3 < len && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                scLen = 4;
                return (int64_t)i;
            }
        }
    }
    return -1;
}

CodecType BitstreamParser::detectCodec(const uint8_t *data, size_t len) {
    for (size_t i = 0; i + 4 < len; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            uint8_t nalType = 0;
            if (data[i + 2] == 0x01) {
                nalType = data[i + 3] & 0x1F;
            } else if (data[i + 2] == 0x00 && data[i + 3] == 0x01 && i + 4 < len) {
                nalType = data[i + 4] & 0x1F;
            } else {
                continue;
            }
            if (nalType <= 30) return CodecType::H264;
            uint8_t h265Type = (data[i + 3] >> 1) & 0x3F;
            if (h265Type <= 51) return CodecType::H265;
        }
    }
    return CodecType::H264;
}

bool BitstreamParser::parseFile(const std::string &path, const ProgressFn &progress) {
    resetStorage();
    m_error.clear();

    if (!m_mapped.open(path, m_error)) return false;
    m_base = m_mapped.data();
    return scan(m_base, (size_t)m_mapped.size(), progress);
}

bool BitstreamParser::parseBuffer(const uint8_t *data, size_t len) {
    resetStorage();
    m_error.clear();

    if (!data || len < 4) {
        m_error = "Buffer too small";
        return false;
    }
    m_buffer.assign(data, data + len);
    m_base = m_buffer.data();
    return scan(m_base, m_buffer.size(), {});
}

bool BitstreamParser::scan(const uint8_t *data, size_t len, const ProgressFn &progress) {
    if (m_codec == CodecType::H264) {
        m_codec = detectCodec(data, len);
    }

    m_nals.reserve(std::min<size_t>(len / 1024u, 8u * 1024u * 1024u));

    size_t pos = 0;
    uint8_t scLen = 0;
    int64_t firstStart = findStartCode(data, len, pos, scLen);
    if (firstStart < 0) {
        m_error = "No start code found";
        return false;
    }

    size_t currentStart = (size_t)firstStart;
    uint8_t currentScLen = scLen;
    const size_t reportStep = 64u * 1024u * 1024u;
    size_t nextReport = reportStep;

    while (true) {
        if (progress && currentStart >= nextReport) {
            if (!progress((double)currentStart / (double)len)) {
                m_error = "Cancelled";
                return false;
            }
            nextReport = currentStart + reportStep;
        }
        size_t searchFrom = currentStart + currentScLen;
        if (m_codec == CodecType::H264 && searchFrom < len) searchFrom += 1;
        if (m_codec == CodecType::H265 && searchFrom < len) searchFrom += 2;

        uint8_t nextScLen = 0;
        int64_t nextStart = -1;
        if (searchFrom < len) {
            nextStart = findStartCode(data, len, searchFrom, nextScLen);
        }

        const size_t endPos = (nextStart < 0) ? len : (size_t)nextStart;
        const uint32_t nalSize = (uint32_t)(endPos - currentStart);

        NALUnit nal;
        nal.codec = m_codec;
        nal.offset = currentStart;
        nal.size = nalSize;
        nal.startCodeLen = currentScLen;

        if (nalSize > currentScLen) {
            const uint8_t *hdr = data + currentStart + currentScLen;
            nal.headerByte = hdr[0];
            if (m_codec == CodecType::H264) {
                nal.nalType = nal.headerByte & 0x1F;
            } else {
                nal.nalType = (nal.headerByte >> 1) & 0x3F;
                if (nalSize > currentScLen + 1) {
                    nal.nuhLayerId = ((nal.headerByte & 0x80) >> 7) | ((hdr[1] & 0xF8) >> 3);
                    nal.nuhTemporalIdPlus1 = hdr[1] & 0x07;
                }
            }
        }

        m_nals.push_back(nal);

        if (nextStart < 0) break;
        currentStart = (size_t)nextStart;
        currentScLen = nextScLen;
    }

    return true;
}

// NAL type name mappings
static const char *h264_nal_names[] = {
    "UNSPECIFIED", "NON_IDR_SLICE", "PART_A", "PART_B", "PART_C",
    "IDR_SLICE", "SEI", "SPS", "PPS", "AUD",
    "EOSSEQ", "EOSTREAM", "FILLER", "SPSEXT", "RESERVED",
    "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
    "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
    "RESERVED", "RESERVED", "RESERVED", "RESERVED", "RESERVED",
    "RESERVED"
};

static const char *h265_nal_names[] = {
    "TRAIL_N", "TRAIL_R", "TRAILSA_N", "TRAILSA_R", "RADL_N",
    "RADL_R", "BLA_W_LP", "BLA_W_RADL", "BLA_N_LP", "IDR_W_RADL",
    "IDR_N_LP", "CRA_NUT", "RSV_VCL_N12", "RSV_VCL_N13", "RSV_VCL_N14",
    "RSV_VCL_N15", "RSV_VCL_R16", "RSV_VCL_R17", "RSV_VCL_R18", "RSV_VCL_R19",
    "RSV_VCL_R20", "RSV_VCL_R21", "RSV_VCL_R22", "RSV_VCL_R23", "RSV_VCL_R24",
    "RSV_VCL_R25", "RSV_VCL_R26", "RSV_VCL_R27", "RSV_VCL_R28", "RSV_VCL_R29",
    "RSV_VCL_R30", "RSV_VCL_R31", "VPS", "SPS", "PPS",
    "AUD", "EOS_NUT", "EOB_NUT", "PREFIX_SEI", "SUFFIX_SEI",
    "RSV_NVCL_40", "RSV_NVCL_41", "RSV_NVCL_42", "RSV_NVCL_43", "RSV_NVCL_44",
    "RSV_NVCL_45", "RSV_NVCL_46", "RSV_NVCL_47", "UNSPECIFIED"
};

std::string NALUnit::typeName() const {
    if (codec == CodecType::H264) {
        if (nalType <= 30) return h264_nal_names[nalType];
        return "RESERVED";
    }
    if (nalType <= 40) return h265_nal_names[nalType];
    return "UNSPECIFIED";
}

std::string NALUnit::summary() const {
    char buf[128];
    if (codec == CodecType::H264) {
        snprintf(buf, sizeof(buf), "H.264 | offset=0x%llX | size=%u | type=%u %s",
                 (unsigned long long)offset, size, nalType, typeName().c_str());
    } else {
        snprintf(buf, sizeof(buf), "H.265 | offset=0x%llX | size=%u | type=%u %s (layer=%u tempId=%u)",
                 (unsigned long long)offset, size, nalType, typeName().c_str(),
                 nuhLayerId, nuhTemporalIdPlus1);
    }
    return std::string(buf);
}
