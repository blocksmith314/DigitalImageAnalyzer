#include "bitstreamparser.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

BitstreamParser::BitstreamParser(CodecType codec) : codec_(codec) {}

BitstreamParser::~BitstreamParser() { ResetStorage(); }

BitstreamParser::BitstreamParser(BitstreamParser&& other) noexcept
    : codec_(other.codec_),
      nals_(std::move(other.nals_)),
      error_(std::move(other.error_)),
      buffer_(std::move(other.buffer_)),
      mapped_(std::move(other.mapped_)),
      base_(other.base_) {
  other.base_ = nullptr;
}

BitstreamParser& BitstreamParser::operator=(BitstreamParser&& other) noexcept {
  if (this != &other) {
    ResetStorage();
    codec_ = other.codec_;
    nals_ = std::move(other.nals_);
    error_ = std::move(other.error_);
    buffer_ = std::move(other.buffer_);
    mapped_ = std::move(other.mapped_);
    base_ = other.base_;
    other.base_ = nullptr;
  }
  return *this;
}

void BitstreamParser::ReleaseMappedPages() {
  mapped_.ReleaseResidentPages();
}

void BitstreamParser::ResetStorage() {
  mapped_.close();
  base_ = nullptr;
  buffer_.clear();
  extra_.clear();
  nals_.clear();
}

static int64_t find_start_code(const uint8_t* data, size_t len, size_t pos,
                               uint8_t& sc_len) {
  for (size_t i = pos; i + 3 < len; i++) {
    if (data[i] == 0x00 && data[i + 1] == 0x00) {
      if (data[i + 2] == 0x01) {
        sc_len = 3;
        return (int64_t)i;
      }
      if (i + 3 < len && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
        sc_len = 4;
        return (int64_t)i;
      }
    }
  }
  return -1;
}

CodecType BitstreamParser::DetectCodec(const uint8_t* data, size_t len) {
  // Look at several NAL headers and score both interpretations. A single
  // header is not enough: an HEVC VPS header (0x40) also looks like an
  // H.264 NAL type 0.
  int h264_score = 0;
  int h265_score = 0;
  int inspected = 0;
  size_t last_header = SIZE_MAX;

  for (size_t i = 0; i + 3 < len && inspected < 16; i++) {
    size_t header = SIZE_MAX;
    if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
      header = i + 3;
    } else if (i + 4 < len && data[i] == 0x00 && data[i + 1] == 0x00 &&
               data[i + 2] == 0x00 && data[i + 3] == 0x01) {
      header = i + 4;
    }
    if (header == SIZE_MAX || header >= len || header == last_header) continue;
    last_header = header;

    const uint8_t byte = data[header];
    if (byte & 0x80) continue;  // forbidden_zero_bit must be zero

    const uint8_t h264_type = byte & 0x1F;
    const uint8_t h265_type = (byte >> 1) & 0x3F;

    // Parameter sets, SEI and AUD are strong evidence for HEVC.
    if (h265_type == 32 || h265_type == 33 || h265_type == 34 ||
        h265_type == 35 || h265_type == 39 || h265_type == 40) {
      h265_score++;
    }
    // Slice types, SEI, SPS/PPS/AUD are strong evidence for H.264.
    if (h264_type == 1 || h264_type == 5 || h264_type == 6 || h264_type == 7 ||
        h264_type == 8 || h264_type == 9) {
      h264_score++;
    }
    inspected++;
  }

  if (h265_score > h264_score) return CodecType::kH265;
  return CodecType::kH264;
}
bool BitstreamParser::ParseFile(const std::string& path,
                                 const ProgressFn& progress) {
  ResetStorage();
  error_.clear();

  if (!mapped_.open(path, error_)) return false;
  base_ = mapped_.data();
  return scan(base_, (size_t)mapped_.size(), progress);
}

bool BitstreamParser::ParseBuffer(const uint8_t* data, size_t len) {
  ResetStorage();
  error_.clear();

  if (!data || len < 4) {
    error_ = "Buffer too small";
    return false;
  }
  buffer_.assign(data, data + len);
  base_ = buffer_.data();
  return scan(base_, buffer_.size(), {});
}

bool BitstreamParser::scan(const uint8_t* data, size_t len,
                           const ProgressFn& progress) {
  if (codec_ == CodecType::kH264) {
    codec_ = DetectCodec(data, len);
  }

  nals_.reserve(std::min<size_t>(len / 1024u, 8u * 1024u * 1024u));

  size_t pos = 0;
  uint8_t sc_len = 0;
  int64_t first_start = find_start_code(data, len, pos, sc_len);
  if (first_start < 0) {
    error_ = "No start code found";
    return false;
  }

  size_t current_start = (size_t)first_start;
  uint8_t current_sc_len = sc_len;
  const size_t report_step = 64u * 1024u * 1024u;
  size_t next_report = report_step;

  while (true) {
    if (progress && current_start >= next_report) {
      if (!progress((double)current_start / (double)len)) {
        error_ = "Cancelled";
        return false;
      }
      next_report = current_start + report_step;
    }
    size_t search_from = current_start + current_sc_len;
    if (codec_ == CodecType::kH264 && search_from < len) search_from += 1;
    if (codec_ == CodecType::kH265 && search_from < len) search_from += 2;

    uint8_t next_sc_len = 0;
    int64_t next_start = -1;
    if (search_from < len) {
      next_start = find_start_code(data, len, search_from, next_sc_len);
    }

    const size_t end_pos = (next_start < 0) ? len : (size_t)next_start;
    const uint32_t nal_size = (uint32_t)(end_pos - current_start);

    NALUnit nal;
    nal.codec = codec_;
    nal.offset = current_start;
    nal.size = nal_size;
    nal.start_code_len = current_sc_len;

    if (nal_size > current_sc_len) {
      const uint8_t* hdr = data + current_start + current_sc_len;
      nal.header_byte = hdr[0];
      if (codec_ == CodecType::kH264) {
        nal.nal_type = nal.header_byte & 0x1F;
      } else {
        nal.nal_type = (nal.header_byte >> 1) & 0x3F;
        if (nal_size > current_sc_len + 1) {
          nal.nuh_layer_id =
              ((nal.header_byte & 0x80) >> 7) | ((hdr[1] & 0xF8) >> 3);
          nal.nuh_temporal_id_plus1 = hdr[1] & 0x07;
        }
      }
    }

    nals_.push_back(nal);

    if (next_start < 0) break;
    current_start = (size_t)next_start;
    current_sc_len = next_sc_len;
  }

  return true;
}

// NAL type name mappings
static const char* h264_nal_names[] = {
    "UNSPECIFIED", "NON_IDR_SLICE", "PART_A",   "PART_B",   "PART_C",
    "IDR_SLICE",   "SEI",           "SPS",      "PPS",      "AUD",
    "EOSSEQ",      "EOSTREAM",      "FILLER",   "SPSEXT",   "RESERVED",
    "RESERVED",    "RESERVED",      "RESERVED", "RESERVED", "RESERVED",
    "RESERVED",    "RESERVED",      "RESERVED", "RESERVED", "RESERVED",
    "RESERVED",    "RESERVED",      "RESERVED", "RESERVED", "RESERVED",
    "RESERVED"};

static const char* h265_nal_names[] = {
    "TRAIL_N",
    "TRAIL_R",
    "TSA_N",
    "TSA_R",
    "STSA_N",
    "STSA_R",
    "RADL_N",
    "RADL_R",
    "RASL_N",
    "RASL_R",
    "RSV_VCL_N10",
    "RSV_VCL_N11",
    "RSV_VCL_N12",
    "RSV_VCL_N13",
    "RSV_VCL_N14",
    "RSV_VCL_N15",
    "BLA_W_LP",
    "BLA_W_RADL",
    "BLA_N_LP",
    "IDR_W_RADL",
    "IDR_N_LP",
    "CRA_NUT",
    "RSV_IRAP_VCL22",
    "RSV_IRAP_VCL23",
    "RSV_IRAP_VCL24",
    "RSV_IRAP_VCL25",
    "RSV_IRAP_VCL26",
    "RSV_IRAP_VCL27",
    "RSV_IRAP_VCL28",
    "RSV_IRAP_VCL29",
    "RSV_IRAP_VCL30",
    "RSV_IRAP_VCL31",
    "VPS",
    "SPS",
    "PPS",
    "AUD",
    "EOS_NUT",
    "EOB_NUT",
    "FD_NUT",
    "PREFIX_SEI",
    "SUFFIX_SEI",
    "RSV_NVCL41",
    "RSV_NVCL42",
    "RSV_NVCL43",
    "RSV_NVCL44",
    "RSV_NVCL45",
    "RSV_NVCL46",
    "RSV_NVCL47",
};

std::string NALUnit::TypeName() const {
  if (codec == CodecType::kH264) {
    if (nal_type <= 30) return h264_nal_names[nal_type];
    return "RESERVED";
  }
  constexpr size_t k_h265_name_count =
      sizeof(h265_nal_names) / sizeof(h265_nal_names[0]);
  if (nal_type < k_h265_name_count) return h265_nal_names[nal_type];
  return "UNSPECIFIED";
}

std::string NALUnit::summary() const {
  char buf[128];
  if (codec == CodecType::kH264) {
    snprintf(buf, sizeof(buf), "H.264 | offset=0x%llX | size=%u | type=%u %s",
             (unsigned long long)offset, size, nal_type, TypeName().c_str());
  } else {
    snprintf(
        buf, sizeof(buf),
        "H.265 | offset=0x%llX | size=%u | type=%u %s (layer=%u tempId=%u)",
        (unsigned long long)offset, size, nal_type, TypeName().c_str(),
        nuh_layer_id, nuh_temporal_id_plus1);
  }
  return std::string(buf);
}
