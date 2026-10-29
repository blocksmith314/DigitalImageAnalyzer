#include <algorithm>
#include <cstring>

#include "bitstreamparser.h"

namespace {

struct Box {
  uint32_t type = 0;
  uint64_t offset = 0;  // byte offset of the box header
  uint64_t size = 0;    // total box size including the header
};

uint32_t read_u32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | p[3];
}

uint64_t read_u64(const uint8_t* p) {
  return ((uint64_t)read_u32(p) << 32) | read_u32(p + 4);
}

// Iterates boxes inside [pos, end).
bool next_box(const uint8_t* data, uint64_t end, uint64_t& pos, Box& box) {
  if (pos + 8 > end) return false;
  const uint8_t* p = data + pos;
  uint64_t size = read_u32(p);
  const uint32_t type = read_u32(p + 4);
  uint64_t header = 8;
  if (size == 1) {
    if (pos + 16 > end) return false;
    size = read_u64(p + 8);
    header = 16;
  } else if (size == 0) {
    size = end - pos;
  }
  if (size < header || pos + size > end) return false;
  box.type = type;
  box.offset = pos;
  box.size = size;
  pos += size;
  return true;
}

bool find_box(const uint8_t* data, uint64_t start, uint64_t end,
              uint32_t wanted, Box& out) {
  uint64_t pos = start;
  Box box;
  while (next_box(data, end, pos, box)) {
    if (box.type == wanted) {
      out = box;
      return true;
    }
  }
  return false;
}

bool append_extra_nal_unit(std::vector<NALUnit>& nals,
                           std::vector<uint8_t>& extra, uint64_t extra_base,
                           const uint8_t* data, int len, CodecType codec) {
  if (len < 1) return false;
  NALUnit nal;
  nal.codec = codec;
  nal.offset = extra_base + extra.size();
  nal.size = (uint32_t)len;
  nal.start_code_len = 0;
  nal.header_byte = data[0];
  nal.nal_type =
      (codec == CodecType::kH264) ? (data[0] & 0x1F) : ((data[0] >> 1) & 0x3F);
  if (codec == CodecType::kH265 && len > 1) {
    nal.nuh_layer_id = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
    nal.nuh_temporal_id_plus1 = data[1] & 0x07;
  }
  extra.insert(extra.end(), data, data + len);
  nals.push_back(nal);
  return true;
}

void appendExtradata(const uint8_t* ex, int size, CodecType codec,
                     std::vector<NALUnit>& nals, std::vector<uint8_t>& extra,
                     uint64_t extra_base, int& length_size) {
  length_size = 4;
  if (!ex || size <= 5 || ex[0] != 1) return;

  size_t pos;
  if (codec == CodecType::kH264) {
    length_size = (ex[4] & 0x03) + 1;
    pos = 5;
    if (pos < (size_t)size) {
      const int n = ex[pos++] & 0x1F;
      for (int i = 0; i < n && pos + 2 <= (size_t)size; i++) {
        const int len = (ex[pos] << 8) | ex[pos + 1];
        pos += 2;
        if (len <= 0 || pos + len > (size_t)size) return;
        append_extra_nal_unit(nals, extra, extra_base, ex + pos, len, codec);
        pos += len;
      }
      if (pos < (size_t)size) {
        const int n2 = ex[pos++];
        for (int i = 0; i < n2 && pos + 2 <= (size_t)size; i++) {
          const int len = (ex[pos] << 8) | ex[pos + 1];
          pos += 2;
          if (len <= 0 || pos + len > (size_t)size) return;
          append_extra_nal_unit(nals, extra, extra_base, ex + pos, len, codec);
          pos += len;
        }
      }
    }
  } else {
    if (size < 23) return;
    length_size = (ex[21] & 0x03) + 1;
    pos = 22;
    const int arrays = ex[pos++];
    for (int a = 0; a < arrays && pos + 3 <= (size_t)size; a++) {
      pos += 1;
      const int n = (ex[pos] << 8) | ex[pos + 1];
      pos += 2;
      for (int i = 0; i < n && pos + 2 <= (size_t)size; i++) {
        const int len = (ex[pos] << 8) | ex[pos + 1];
        pos += 2;
        if (len <= 0 || pos + len > (size_t)size) return;
        append_extra_nal_unit(nals, extra, extra_base, ex + pos, len, codec);
        pos += len;
      }
    }
  }
}

}  // namespace

bool BitstreamParser::ParseMp4Indexed(const std::string& path,
                                        CodecType& codec,
                                        const ProgressFn& progress) {
  ResetStorage();
  error_.clear();

  if (!mapped_.open(path, error_)) return false;
  base_ = mapped_.data();

  const uint8_t* data = base_;
  const uint64_t file_size = mapped_.size();

  // ---- locate moov ----
  Box moov;
  bool have_moov = false;
  uint64_t top = 0;
  Box b;
  while (next_box(data, file_size, top, b)) {
    if (b.type == 0x6D6F6F76u) {  // 'moov'
      moov = b;
      have_moov = true;
      break;
    }
  }
  if (!have_moov) {
    error_ = "Not an MP4 with moov";
    ResetStorage();
    return false;
  }

  // ---- pick the video track ----
  const uint64_t moov_end = moov.offset + moov.size;
  const uint64_t moov_body = moov.offset + 8;
  bool found_video = false;
  Box stszBox, stcoBox, co64Box, stscBox, stsdBox;
  std::vector<Box> traks;
  {
    uint64_t pos = moov_body;
    Box box;
    while (next_box(data, moov_end, pos, box)) {
      if (box.type == 0x7472616Bu) traks.push_back(box);  // 'trak'
    }
  }

  for (const Box& trak : traks) {
    const uint64_t trak_end = trak.offset + trak.size;
    Box mdia;
    if (!find_box(data, trak.offset + 8, trak_end, 0x6D646961u, mdia))
      continue;  // 'mdia'

    const uint64_t mdia_end = mdia.offset + mdia.size;
    // handler type must be 'vide'
    Box hdlr;
    if (!find_box(data, mdia.offset + 8, mdia_end, 0x68646C72u, hdlr))
      continue;  // 'hdlr'
    if (hdlr.offset + 8 + 8 + 4 > file_size) continue;
    if (read_u32(data + hdlr.offset + 16) != 0x76696465u) continue;  // 'vide'

    Box minf;
    if (!find_box(data, mdia.offset + 8, mdia_end, 0x6D696E66u, minf))
      continue;  // 'minf'
    Box stbl;
    if (!find_box(data, minf.offset + 8, minf.offset + minf.size, 0x7374626Cu,
                  stbl))
      continue;  // 'stbl'

    const uint64_t stbl_end = stbl.offset + stbl.size;
    if (!find_box(data, stbl.offset + 8, stbl_end, 0x7374737Au, stszBox))
      continue;  // 'stsz'
    if (!find_box(data, stbl.offset + 8, stbl_end, 0x73747363u, stscBox))
      continue;  // 'stsc'
    found_video = find_box(data, stbl.offset + 8, stbl_end, 0x73747364u,
                           stsdBox);  // 'stsd'
    if (!find_box(data, stbl.offset + 8, stbl_end, 0x7374636Fu,
                  stcoBox)) {  // 'stco'
      if (!find_box(data, stbl.offset + 8, stbl_end, 0x636F3634u,
                    co64Box)) {  // 'co64'
        found_video = false;
      }
    }
    if (found_video) break;
  }

  if (!found_video) {
    error_ = "No indexed video track";
    ResetStorage();
    return false;
  }

  // ---- codec + extradata from the first visual sample entry ----
  if (stsdBox.size < 16) {
    error_ = "Bad stsd";
    ResetStorage();
    return false;
  }
  const uint64_t entry_start =
      stsdBox.offset + 16;  // version/flags + entry count
  const uint64_t stsd_end = stsdBox.offset + stsdBox.size;
  if (entry_start + 8 > stsd_end) {
    error_ = "Bad stsd entry";
    ResetStorage();
    return false;
  }
  const uint32_t entry_type = read_u32(data + entry_start + 4);
  if (entry_type == 0x61766331u || entry_type == 0x61766333u) {  // avc1/avc3
    codec = CodecType::kH264;
  } else if (entry_type == 0x68766331u ||
             entry_type == 0x68657631u) {  // hvc1/hev1
    codec = CodecType::kH265;
  } else {
    error_ = "Unsupported video codec (only H.264/H.265)";
    ResetStorage();
    return false;
  }
  codec_ = codec;

  Box cfg;
  const uint64_t child_start =
      entry_start + 8 + 78;  // visual sample entry header
  if (!find_box(data, child_start, stsd_end,
                codec == CodecType::kH264 ? 0x61766343u : 0x68766343u, cfg)) {
    error_ = "Missing avcC/hvcC (fragmented MP4?)";
    ResetStorage();
    return false;
  }
  int length_size = 4;
  appendExtradata(data + cfg.offset + 8, (int)(cfg.size - 8), codec, nals_,
                  extra_, mapped_.size(), length_size);

  // ---- sample sizes ----
  if (stszBox.size < 20) {
    error_ = "Bad stsz";
    ResetStorage();
    return false;
  }
  const uint8_t* stsz = data + stszBox.offset;
  const uint32_t uniform_size = read_u32(stsz + 12);
  const uint32_t sample_count = read_u32(stsz + 16);
  if (sample_count == 0) {
    error_ = "Empty sample table";
    ResetStorage();
    return false;
  }
  std::vector<uint32_t> sizes;
  if (uniform_size != 0) {
    sizes.assign(sample_count, uniform_size);
  } else {
    if (stszBox.size < 20 + (uint64_t)sample_count * 4) {
      error_ = "Truncated stsz";
      ResetStorage();
      return false;
    }
    sizes.resize(sample_count);
    const uint8_t* tbl = stsz + 20;
    for (uint32_t i = 0; i < sample_count; i++)
      sizes[i] = read_u32(tbl + 4 * i);
  }

  // With small (dense) samples the header pages cover the whole file anyway;
  // a sequential demux is faster in that case.
  uint64_t total_sample_bytes = 0;
  for (uint32_t i = 0; i < sample_count; i++) total_sample_bytes += sizes[i];
  if (sample_count > 0 &&
      (double)total_sample_bytes / (double)sample_count < 4096.0) {
    error_ = "Dense sample table";
    ResetStorage();
    return false;
  }

  // ---- chunk offsets ----
  std::vector<uint64_t> chunk_offsets;
  if (stcoBox.size >= 16) {
    const uint8_t* p = data + stcoBox.offset;
    const uint32_t n = read_u32(p + 12);
    chunk_offsets.resize(n);
    for (uint32_t i = 0; i < n; i++)
      chunk_offsets[i] = read_u32(p + 16 + 4 * i);
  } else if (co64Box.size >= 16) {
    const uint8_t* p = data + co64Box.offset;
    const uint32_t n = read_u32(p + 12);
    chunk_offsets.resize(n);
    for (uint32_t i = 0; i < n; i++)
      chunk_offsets[i] = read_u64(p + 16 + 8 * i);
  }
  if (chunk_offsets.empty()) {
    error_ = "Missing chunk offsets";
    ResetStorage();
    return false;
  }

  // ---- sample-to-chunk ----
  struct StscEntry {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
  };
  std::vector<StscEntry> stsc;
  if (stscBox.size >= 16) {
    const uint8_t* p = data + stscBox.offset;
    const uint32_t n = read_u32(p + 12);
    if (stscBox.size < 16 + (uint64_t)n * 12) {
      error_ = "Truncated stsc";
      ResetStorage();
      return false;
    }
    stsc.resize(n);
    for (uint32_t i = 0; i < n; i++) {
      stsc[i].first_chunk = read_u32(p + 16 + 12 * i);
      stsc[i].samples_per_chunk = read_u32(p + 20 + 12 * i);
    }
  }
  if (stsc.empty()) {
    error_ = "Missing stsc";
    ResetStorage();
    return false;
  }

  // ---- walk samples and build NAL units from length prefixes ----
  uint32_t sample_idx = 0;
  size_t stsc_idx = 0;
  for (uint32_t chunk = 0;
       chunk < chunk_offsets.size() && sample_idx < sample_count; chunk++) {
    while (stsc_idx + 1 < stsc.size() &&
           stsc[stsc_idx + 1].first_chunk <= chunk + 1)
      stsc_idx++;
    uint64_t offset = chunk_offsets[chunk];
    const uint32_t per_chunk = stsc[stsc_idx].samples_per_chunk;
    for (uint32_t k = 0; k < per_chunk && sample_idx < sample_count;
         k++, sample_idx++) {
      const uint32_t sample_size = sizes[sample_idx];
      const uint64_t sample_start = offset;
      offset += sample_size;
      if (sample_start + sample_size > file_size) break;

      size_t rel = 0;
      while (rel + (size_t)length_size <= sample_size) {
        uint32_t nal_len = 0;
        for (int bidx = 0; bidx < length_size; bidx++)
          nal_len = (nal_len << 8) | data[sample_start + rel + bidx];
        rel += (size_t)length_size;
        if (nal_len == 0 || rel + nal_len > sample_size) break;

        const uint8_t* np = data + sample_start + rel;
        NALUnit nal;
        nal.codec = codec;
        nal.offset = sample_start + rel;
        nal.size = nal_len;
        nal.start_code_len = 0;
        nal.header_byte = np[0];
        if (codec == CodecType::kH264) {
          nal.nal_type = np[0] & 0x1F;
        } else {
          nal.nal_type = (np[0] >> 1) & 0x3F;
          if (nal_len > 1) {
            nal.nuh_layer_id = ((np[0] & 0x80) >> 7) | ((np[1] & 0xF8) >> 3);
            nal.nuh_temporal_id_plus1 = np[1] & 0x07;
          }
        }
        nals_.push_back(nal);
        rel += nal_len;
      }

      if (progress && (sample_idx % 5000 == 4999)) {
        if (!progress((double)sample_idx / (double)sample_count)) {
          error_ = "Cancelled";
          ResetStorage();
          return false;
        }
      }
    }
  }

  if (nals_.empty()) {
    error_ = "No NAL units found";
    ResetStorage();
    return false;
  }
  if (progress) progress(1.0);
  return true;
}
