#include <algorithm>
#include <cstring>

#include "bitstreamparser.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {

void appendExtraNal(std::vector<NALUnit>& nals, std::vector<uint8_t>& extra,
                    uint64_t extra_base, const uint8_t* data, int len,
                    CodecType codec) {
  if (len < 1) return;
  NALUnit nal;
  nal.codec = codec;
  nal.offset = extra_base + extra.size();
  nal.size = (uint32_t)len;
  nal.start_code_len = 0;
  nal.header_byte = data[0];
  if (codec == CodecType::kH264) {
    nal.nal_type = data[0] & 0x1F;
  } else {
    nal.nal_type = (data[0] >> 1) & 0x3F;
    if (len > 1) {
      nal.nuh_layer_id = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
      nal.nuh_temporal_id_plus1 = data[1] & 0x07;
    }
  }
  extra.insert(extra.end(), data, data + len);
  nals.push_back(nal);
}

void appendPacketNal(std::vector<NALUnit>& nals, const uint8_t* data,
                     uint32_t len, uint64_t file_offset, CodecType codec) {
  if (len < 1) return;
  NALUnit nal;
  nal.codec = codec;
  nal.offset = file_offset;
  nal.size = len;
  nal.start_code_len = 0;
  nal.header_byte = data[0];
  if (codec == CodecType::kH264) {
    nal.nal_type = data[0] & 0x1F;
  } else {
    nal.nal_type = (data[0] >> 1) & 0x3F;
    if (len > 1) {
      nal.nuh_layer_id = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
      nal.nuh_temporal_id_plus1 = data[1] & 0x07;
    }
  }
  nals.push_back(nal);
}

// Parses the avcC/hvcC extradata and queues the SPS/PPS/VPS NAL units.
void appendExtradataNals(std::vector<NALUnit>& nals,
                         std::vector<uint8_t>& extra, uint64_t extra_base,
                         const AVCodecParameters* par, CodecType codec,
                         int& length_size) {
  const uint8_t* ex = par->extradata;
  const int size = par->extradata_size;
  if (!ex || size <= 4 || ex[0] != 1) return;

  length_size = 4;
  size_t pos = 0;

  if (codec == CodecType::kH264) {
    length_size = (ex[4] & 0x03) + 1;
    pos = 5;
    if (pos < (size_t)size) {
      const int num_sps = ex[pos++] & 0x1F;
      for (int i = 0; i < num_sps && pos + 2 <= (size_t)size; i++) {
        const int len = (ex[pos] << 8) | ex[pos + 1];
        pos += 2;
        if (len <= 0 || pos + len > (size_t)size) return;
        appendExtraNal(nals, extra, extra_base, ex + pos, len, codec);
        pos += len;
      }
      if (pos < (size_t)size) {
        const int num_pps = ex[pos++];
        for (int i = 0; i < num_pps && pos + 2 <= (size_t)size; i++) {
          const int len = (ex[pos] << 8) | ex[pos + 1];
          pos += 2;
          if (len <= 0 || pos + len > (size_t)size) return;
          appendExtraNal(nals, extra, extra_base, ex + pos, len, codec);
          pos += len;
        }
      }
    }
  } else {
    if (size < 23) return;
    length_size = (ex[21] & 0x03) + 1;
    pos = 22;
    const int num_arrays = ex[pos++];
    for (int a = 0; a < num_arrays && pos + 3 <= (size_t)size; a++) {
      pos += 1;  // array_completeness + reserved + NAL unit type
      const int num_nalus = (ex[pos] << 8) | ex[pos + 1];
      pos += 2;
      for (int i = 0; i < num_nalus && pos + 2 <= (size_t)size; i++) {
        const int len = (ex[pos] << 8) | ex[pos + 1];
        pos += 2;
        if (len <= 0 || pos + len > (size_t)size) return;
        appendExtraNal(nals, extra, extra_base, ex + pos, len, codec);
        pos += len;
      }
    }
  }
}

}  // namespace

bool BitstreamParser::ParseAvcc(const std::string& path, CodecType& codec,
                                 const ProgressFn& progress) {
  ResetStorage();
  error_.clear();

  if (!mapped_.open(path, error_)) return false;
  base_ = mapped_.data();

  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    error_ = "Failed to open input";
    ResetStorage();
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    error_ = "Failed to find stream info";
    ResetStorage();
    return false;
  }

  int video_idx = -1;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = (int)i;
      break;
    }
  }
  if (video_idx < 0) {
    avformat_close_input(&fmt);
    error_ = "No video stream found";
    ResetStorage();
    return false;
  }

  AVStream* stream = fmt->streams[video_idx];
  AVCodecParameters* par = stream->codecpar;
  if (par->codec_id == AV_CODEC_ID_H264) {
    codec = CodecType::kH264;
  } else if (par->codec_id == AV_CODEC_ID_HEVC) {
    codec = CodecType::kH265;
  } else {
    avformat_close_input(&fmt);
    error_ = "Unsupported video codec (only H.264/H.265)";
    ResetStorage();
    return false;
  }
  codec_ = codec;

  int length_size = 4;
  appendExtradataNals(nals_, extra_, mapped_.size(), par, codec, length_size);

  AVPacket* pkt = av_packet_alloc();
  const int64_t total_frames = (stream->nb_frames > 0) ? stream->nb_frames : 0;
  int64_t seen = 0;
  bool cancelled = false;
  bool bad_pos = false;

  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == video_idx && pkt->size > 0) {
      if (pkt->pos < 0) {
        bad_pos = true;
        av_packet_unref(pkt);
        break;
      }
      size_t rel = 0;
      const size_t end = (size_t)pkt->size;
      while (rel + (size_t)length_size <= end) {
        uint32_t nal_len = 0;
        for (int b = 0; b < length_size; b++)
          nal_len = (nal_len << 8) | pkt->data[rel + b];
        rel += (size_t)length_size;
        if (nal_len == 0 || rel + nal_len > end) break;
        appendPacketNal(nals_, pkt->data + rel, nal_len,
                        (uint64_t)pkt->pos + rel, codec);
        rel += nal_len;
      }
      if (progress && (++seen % 200 == 0)) {
        const double f =
            total_frames > 0
                ? std::min(1.0, (double)seen / (double)total_frames)
                : -1.0;
        if (!progress(f)) {
          cancelled = true;
          av_packet_unref(pkt);
          break;
        }
      }
    }
    av_packet_unref(pkt);
  }

  av_packet_free(&pkt);
  avformat_close_input(&fmt);

  if (cancelled) {
    error_ = "Cancelled";
    ResetStorage();
    return false;
  }
  if (bad_pos) {
    error_ = "Packet offsets unavailable";
    ResetStorage();
    return false;
  }
  if (nals_.empty()) {
    error_ = "No NAL units found";
    ResetStorage();
    return false;
  }
  if (progress) progress(1.0);
  return true;
}
