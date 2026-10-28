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
    // Look at several NAL headers and score both interpretations. A single
    // header is not enough: an HEVC VPS header (0x40) also looks like an
    // H.264 NAL type 0.
    int h264Score = 0;
    int h265Score = 0;
    int inspected = 0;
    size_t lastHeader = SIZE_MAX;

    for (size_t i = 0; i + 3 < len && inspected < 16; i++) {
        size_t header = SIZE_MAX;
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            header = i + 3;
        } else if (i + 4 < len && data[i] == 0x00 && data[i + 1] == 0x00 &&
                   data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            header = i + 4;
        }
        if (header == SIZE_MAX || header >= len || header == lastHeader) continue;
        lastHeader = header;

        const uint8_t byte = data[header];
        if (byte & 0x80) continue;  // forbidden_zero_bit must be zero

        const uint8_t h264Type = byte & 0x1F;
        const uint8_t h265Type = (byte >> 1) & 0x3F;

        // Parameter sets, SEI and AUD are strong evidence for HEVC.
        if (h265Type == 32 || h265Type == 33 || h265Type == 34 ||
            h265Type == 35 || h265Type == 39 || h265Type == 40) {
            h265Score++;
        }
        // Slice types, SEI, SPS/PPS/AUD are strong evidence for H.264.
        if (h264Type == 1 || h264Type == 5 || h264Type == 6 ||
            h264Type == 7 || h264Type == 8 || h264Type == 9) {
            h264Score++;
        }
        inspected++;
    }

    if (h265Score > h264Score) return CodecType::H265;
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
    "TRAIL_N", "TRAIL_R", "TSA_N", "TSA_R", "STSA_N", "STSA_R",
    "RADL_N", "RADL_R", "RASL_N", "RASL_R",
    "RSV_VCL_N10", "RSV_VCL_N11", "RSV_VCL_N12", "RSV_VCL_N13",
    "RSV_VCL_N14", "RSV_VCL_N15",
    "BLA_W_LP", "BLA_W_RADL", "BLA_N_LP", "IDR_W_RADL", "IDR_N_LP",
    "CRA_NUT",
    "RSV_IRAP_VCL22", "RSV_IRAP_VCL23", "RSV_IRAP_VCL24",
    "RSV_IRAP_VCL25", "RSV_IRAP_VCL26", "RSV_IRAP_VCL27",
    "RSV_IRAP_VCL28", "RSV_IRAP_VCL29", "RSV_IRAP_VCL30",
    "RSV_IRAP_VCL31",
    "VPS", "SPS", "PPS", "AUD", "EOS_NUT", "EOB_NUT", "FD_NUT",
    "PREFIX_SEI", "SUFFIX_SEI",
    "RSV_NVCL41", "RSV_NVCL42", "RSV_NVCL43", "RSV_NVCL44",
    "RSV_NVCL45", "RSV_NVCL46", "RSV_NVCL47",
};

std::string NALUnit::typeName() const {
    if (codec == CodecType::H264) {
        if (nalType <= 30) return h264_nal_names[nalType];
        return "RESERVED";
    }
    constexpr size_t kH265NameCount = sizeof(h265_nal_names) / sizeof(h265_nal_names[0]);
    if (nalType < kH265NameCount) return h265_nal_names[nalType];
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
