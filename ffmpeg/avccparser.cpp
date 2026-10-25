#include "bitstreamparser.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace {

void appendExtraNal(std::vector<NALUnit> &nals, std::vector<uint8_t> &extra,
                    uint64_t extraBase, const uint8_t *data, int len, CodecType codec) {
    if (len < 1) return;
    NALUnit nal;
    nal.codec = codec;
    nal.offset = extraBase + extra.size();
    nal.size = (uint32_t)len;
    nal.startCodeLen = 0;
    nal.headerByte = data[0];
    if (codec == CodecType::H264) {
        nal.nalType = data[0] & 0x1F;
    } else {
        nal.nalType = (data[0] >> 1) & 0x3F;
        if (len > 1) {
            nal.nuhLayerId = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
            nal.nuhTemporalIdPlus1 = data[1] & 0x07;
        }
    }
    extra.insert(extra.end(), data, data + len);
    nals.push_back(nal);
}

void appendPacketNal(std::vector<NALUnit> &nals, const uint8_t *data, uint32_t len,
                     uint64_t fileOffset, CodecType codec) {
    if (len < 1) return;
    NALUnit nal;
    nal.codec = codec;
    nal.offset = fileOffset;
    nal.size = len;
    nal.startCodeLen = 0;
    nal.headerByte = data[0];
    if (codec == CodecType::H264) {
        nal.nalType = data[0] & 0x1F;
    } else {
        nal.nalType = (data[0] >> 1) & 0x3F;
        if (len > 1) {
            nal.nuhLayerId = ((data[0] & 0x80) >> 7) | ((data[1] & 0xF8) >> 3);
            nal.nuhTemporalIdPlus1 = data[1] & 0x07;
        }
    }
    nals.push_back(nal);
}

// Parses the avcC/hvcC extradata and queues the SPS/PPS/VPS NAL units.
void appendExtradataNals(std::vector<NALUnit> &nals, std::vector<uint8_t> &extra,
                         uint64_t extraBase, const AVCodecParameters *par, CodecType codec,
                         int &lengthSize) {
    const uint8_t *ex = par->extradata;
    const int size = par->extradata_size;
    if (!ex || size <= 4 || ex[0] != 1) return;

    lengthSize = 4;
    size_t pos = 0;

    if (codec == CodecType::H264) {
        lengthSize = (ex[4] & 0x03) + 1;
        pos = 5;
        if (pos < (size_t)size) {
            const int numSps = ex[pos++] & 0x1F;
            for (int i = 0; i < numSps && pos + 2 <= (size_t)size; i++) {
                const int len = (ex[pos] << 8) | ex[pos + 1];
                pos += 2;
                if (len <= 0 || pos + len > (size_t)size) return;
                appendExtraNal(nals, extra, extraBase, ex + pos, len, codec);
                pos += len;
            }
            if (pos < (size_t)size) {
                const int numPps = ex[pos++];
                for (int i = 0; i < numPps && pos + 2 <= (size_t)size; i++) {
                    const int len = (ex[pos] << 8) | ex[pos + 1];
                    pos += 2;
                    if (len <= 0 || pos + len > (size_t)size) return;
                    appendExtraNal(nals, extra, extraBase, ex + pos, len, codec);
                    pos += len;
                }
            }
        }
    } else {
        if (size < 23) return;
        lengthSize = (ex[21] & 0x03) + 1;
        pos = 22;
        const int numArrays = ex[pos++];
        for (int a = 0; a < numArrays && pos + 3 <= (size_t)size; a++) {
            pos += 1; // array_completeness + reserved + NAL unit type
            const int numNalus = (ex[pos] << 8) | ex[pos + 1];
            pos += 2;
            for (int i = 0; i < numNalus && pos + 2 <= (size_t)size; i++) {
                const int len = (ex[pos] << 8) | ex[pos + 1];
                pos += 2;
                if (len <= 0 || pos + len > (size_t)size) return;
                appendExtraNal(nals, extra, extraBase, ex + pos, len, codec);
                pos += len;
            }
        }
    }
}

} // namespace

bool BitstreamParser::parseAvcc(const std::string &path, CodecType &codec,
                                const ProgressFn &progress) {
    resetStorage();
    m_error.clear();

    if (!m_mapped.open(path, m_error)) return false;
    m_base = m_mapped.data();

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        m_error = "Failed to open input";
        resetStorage();
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        m_error = "Failed to find stream info";
        resetStorage();
        return false;
    }

    int videoIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoIdx = (int)i;
            break;
        }
    }
    if (videoIdx < 0) {
        avformat_close_input(&fmt);
        m_error = "No video stream found";
        resetStorage();
        return false;
    }

    AVStream *stream = fmt->streams[videoIdx];
    AVCodecParameters *par = stream->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264) {
        codec = CodecType::H264;
    } else if (par->codec_id == AV_CODEC_ID_HEVC) {
        codec = CodecType::H265;
    } else {
        avformat_close_input(&fmt);
        m_error = "Unsupported video codec (only H.264/H.265)";
        resetStorage();
        return false;
    }
    m_codec = codec;

    int lengthSize = 4;
    appendExtradataNals(m_nals, m_extra, m_mapped.size(), par, codec, lengthSize);

    AVPacket *pkt = av_packet_alloc();
    const int64_t totalFrames = (stream->nb_frames > 0) ? stream->nb_frames : 0;
    int64_t seen = 0;
    bool cancelled = false;
    bool badPos = false;

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == videoIdx && pkt->size > 0) {
            if (pkt->pos < 0) {
                badPos = true;
                av_packet_unref(pkt);
                break;
            }
            size_t rel = 0;
            const size_t end = (size_t)pkt->size;
            while (rel + (size_t)lengthSize <= end) {
                uint32_t nalLen = 0;
                for (int b = 0; b < lengthSize; b++) nalLen = (nalLen << 8) | pkt->data[rel + b];
                rel += (size_t)lengthSize;
                if (nalLen == 0 || rel + nalLen > end) break;
                appendPacketNal(m_nals, pkt->data + rel, nalLen,
                                (uint64_t)pkt->pos + rel, codec);
                rel += nalLen;
            }
            if (progress && (++seen % 200 == 0)) {
                const double f = totalFrames > 0
                                     ? std::min(1.0, (double)seen / (double)totalFrames)
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
        m_error = "Cancelled";
        resetStorage();
        return false;
    }
    if (badPos) {
        m_error = "Packet offsets unavailable";
        resetStorage();
        return false;
    }
    if (m_nals.empty()) {
        m_error = "No NAL units found";
        resetStorage();
        return false;
    }
    if (progress) progress(1.0);
    return true;
}
