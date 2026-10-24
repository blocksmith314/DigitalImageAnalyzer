#include "decoder.h"
#include <QImage>
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct FFmpegDecoder::Impl {
    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    SwsContext *swsCtx = nullptr;
    uint8_t *rgbBuffer = nullptr;
    int rgbBufferSize = 0;
    int videoStreamIdx = -1;
};

FFmpegDecoder::FFmpegDecoder() : m_impl(new Impl()) {}

FFmpegDecoder::~FFmpegDecoder() {
    close();
    delete m_impl;
}

bool FFmpegDecoder::openFile(const std::string &path, CodecType codec) {
    close();
    m_impl = new Impl();

    int codecId = (codec == CodecType::H264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;

    const AVInputFormat *fmt = nullptr;
    if (codec == CodecType::H264) {
        fmt = av_find_input_format("h264");
    } else {
        fmt = av_find_input_format("hevc");
    }

    if (avformat_open_input(&m_impl->fmtCtx, path.c_str(), fmt, nullptr) != 0) {
        m_error = "Failed to open input";
        close();
        return false;
    }

    if (avformat_find_stream_info(m_impl->fmtCtx, nullptr) < 0) {
        m_error = "Failed to find stream info";
        close();
        return false;
    }

    m_impl->videoStreamIdx = -1;
    for (unsigned int i = 0; i < m_impl->fmtCtx->nb_streams; i++) {
        if (m_impl->fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_impl->videoStreamIdx = i;
            break;
        }
    }

    if (m_impl->videoStreamIdx < 0) {
        m_error = "No video stream found";
        close();
        return false;
    }

    AVStream *stream = m_impl->fmtCtx->streams[m_impl->videoStreamIdx];
    const AVCodec *codecPtr = avcodec_find_decoder((AVCodecID)codecId);
    if (!codecPtr) {
        m_error = "Codec not found";
        close();
        return false;
    }

    m_impl->codecCtx = avcodec_alloc_context3(codecPtr);
    if (!m_impl->codecCtx) {
        m_error = "Failed to alloc codec ctx";
        close();
        return false;
    }

    if (avcodec_parameters_to_context(m_impl->codecCtx, stream->codecpar) < 0) {
        m_error = "Failed to copy codec params";
        close();
        return false;
    }

    if (avcodec_open2(m_impl->codecCtx, codecPtr, nullptr) < 0) {
        m_error = "Failed to open codec";
        close();
        return false;
    }

    m_width = m_impl->codecCtx->width;
    m_height = m_impl->codecCtx->height;

    m_impl->rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGB24, m_width, m_height, 1);
    m_impl->rgbBuffer = (uint8_t *)av_malloc(m_impl->rgbBufferSize);

    if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
        m_frameRate = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den && stream->r_frame_rate.num) {
        m_frameRate = av_q2d(stream->r_frame_rate);
    }
    if (m_frameRate <= 0) m_frameRate = 30.0;

    m_path = path;
    m_codec = codec;
    m_frameCount = 0;
    m_currentFrame = 0;
    return true;
}

void FFmpegDecoder::close() {
    if (m_impl) {
        if (m_impl->swsCtx) {
            sws_freeContext(m_impl->swsCtx);
            m_impl->swsCtx = nullptr;
        }
        if (m_impl->rgbBuffer) {
            av_free(m_impl->rgbBuffer);
            m_impl->rgbBuffer = nullptr;
        }
        if (m_impl->codecCtx) {
            avcodec_free_context(&m_impl->codecCtx);
            m_impl->codecCtx = nullptr;
        }
        if (m_impl->fmtCtx) {
            avformat_close_input(&m_impl->fmtCtx);
            m_impl->fmtCtx = nullptr;
        }
    }
}

bool FFmpegDecoder::decodeNextFrame(DecodedFrame &out) {
    if (!m_impl || !m_impl->codecCtx || !m_impl->fmtCtx) return false;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool gotFrame = false;

    while (av_read_frame(m_impl->fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != m_impl->videoStreamIdx) {
            av_packet_unref(pkt);
            continue;
        }

        if (avcodec_send_packet(m_impl->codecCtx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }

        while (avcodec_receive_frame(m_impl->codecCtx, frame) >= 0) {
            if (!m_impl->swsCtx) {
                m_impl->swsCtx = sws_getContext(
                    m_width, m_height, m_impl->codecCtx->pix_fmt,
                    m_width, m_height, AV_PIX_FMT_RGB24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
            }

            uint8_t *dst[1] = {m_impl->rgbBuffer};
            int dstLinesize[1] = {m_width * 3};
            sws_scale(m_impl->swsCtx, frame->data, frame->linesize, 0, m_height, dst, dstLinesize);

            QImage img(m_impl->rgbBuffer, m_width, m_height, QImage::Format_RGB888);

            out.pts = frame->pts;
            out.width = m_width;
            out.height = m_height;
            out.image = img.copy();

            m_frameCount++;
            m_currentFrame++;
            gotFrame = true;

            av_packet_unref(pkt);
            av_frame_unref(frame);
            av_packet_free(&pkt);
            av_frame_free(&frame);
            return true;
        }
        av_packet_unref(pkt);
    }

    // flush
    avcodec_send_packet(m_impl->codecCtx, nullptr);
    if (avcodec_receive_frame(m_impl->codecCtx, frame) >= 0) {
        if (!m_impl->swsCtx) {
            m_impl->swsCtx = sws_getContext(
                m_width, m_height, m_impl->codecCtx->pix_fmt,
                m_width, m_height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        }
        uint8_t *dst[1] = {m_impl->rgbBuffer};
        int dstLinesize[1] = {m_width * 3};
        sws_scale(m_impl->swsCtx, frame->data, frame->linesize, 0, m_height, dst, dstLinesize);
        QImage img(m_impl->rgbBuffer, m_width, m_height, QImage::Format_RGB888);
        out.pts = frame->pts;
        out.width = m_width;
        out.height = m_height;
        out.image = img.copy();
        m_frameCount++;
        m_currentFrame++;
        gotFrame = true;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return gotFrame;
}

bool FFmpegDecoder::decodeKeyframes(std::vector<DecodedFrame> &out, int maxFrames) {
    if (!m_impl || !m_impl->codecCtx || !m_impl->fmtCtx) return false;

    int decoded = 0;
    DecodedFrame frame;
    while (decoded < maxFrames && decodeNextFrame(frame)) {
        out.push_back(std::move(frame));
        decoded++;
    }
    return decoded > 0;
}

void FFmpegDecoder::reset() {
    if (!m_path.empty()) {
        openFile(m_path, m_codec);
    }
}

bool FFmpegDecoder::seekToFrame(int targetIndex) {
    if (targetIndex < m_currentFrame) {
        reset();
        if (targetIndex == 0) return true;
    }
    DecodedFrame dummy;
    while (m_currentFrame < targetIndex) {
        if (!decodeNextFrame(dummy)) return false;
    }
    return true;
}

std::unordered_map<int64_t, int> FFmpegDecoder::buildPktPosToDisplayIndexMap() {
    std::unordered_map<int64_t, int> result;
    FILE *fp = fopen("/tmp/da_debug.txt", "w");
    auto LOG = [&](const char *fmt, ...) {
        if (!fp) return;
        va_list ap; va_start(ap, fmt);
        vfprintf(fp, fmt, ap);
        va_end(ap);
    };

    if (!m_impl || !m_impl->codecCtx || !m_impl->fmtCtx) {
        LOG("RETURN EMPTY: no impl\n");
        if (fp) fclose(fp);
        return result;
    }

    LOG("START buildPktPosToDisplayIndexMap\n");

    avformat_seek_file(m_impl->fmtCtx, -1, INT64_MIN, 0, INT64_MAX, 0);
    avcodec_flush_buffers(m_impl->codecCtx);

    std::vector<std::pair<int64_t, int>> pktPosList;
    int dtsIdx = 0;

    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(m_impl->fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == m_impl->videoStreamIdx) {
            pktPosList.push_back({pkt->pos, dtsIdx});
            pkt->pts = dtsIdx;
            pkt->dts = dtsIdx;
            LOG("PKT dtsIdx=%d pos=%lld pkt.pts=%lld size=%d\n",
                    dtsIdx, (long long)pkt->pos, (long long)pkt->pts, pkt->size);
            avcodec_send_packet(m_impl->codecCtx, pkt);
            dtsIdx++;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avcodec_send_packet(m_impl->codecCtx, nullptr);

    std::unordered_map<int64_t, int> ptsToDisplay;
    AVFrame *frame = av_frame_alloc();
    int displayIdx = 0;
    while (avcodec_receive_frame(m_impl->codecCtx, frame) >= 0) {
        ptsToDisplay[frame->pts] = displayIdx;
        LOG("FRAME dispIdx=%d frame.pts=%lld pict_type=%d\n",
                displayIdx, (long long)frame->pts, frame->pict_type);
        displayIdx++;
        av_frame_unref(frame);
    }
    LOG("TOTAL: displayIdx=%d pktCount=%zu ptsMapSize=%zu\n",
            displayIdx, pktPosList.size(), ptsToDisplay.size());
    av_frame_free(&frame);

    for (auto &[pos, didx] : pktPosList) {
        auto it = ptsToDisplay.find(didx);
        if (it != ptsToDisplay.end()) {
            result[pos] = it->second;
        } else {
            LOG("MISS: pos=%lld dtsIdx=%d not found in ptsMap\n",
                    (long long)pos, didx);
        }
    }
    LOG("RESULT map size=%zu\n", result.size());
    for (auto &[pos, disp] : result) {
        LOG("  pos=%lld -> displayIdx=%d\n", (long long)pos, disp);
    }

    avformat_seek_file(m_impl->fmtCtx, -1, INT64_MIN, 0, INT64_MAX, 0);
    avcodec_flush_buffers(m_impl->codecCtx);

    if (fp) fclose(fp);
    return result;
}

std::unordered_map<int64_t, int> FFmpegDecoder::buildPosToDisplayIndexMapFromNals(const std::vector<NALUnit> &nals) {
    std::unordered_map<int64_t, int> result;
    if (nals.empty()) return result;

    int codecId = (m_codec == CodecType::H264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;
    const AVCodec *c = avcodec_find_decoder((AVCodecID)codecId);
    if (!c) return result;

    AVCodecContext *cc = avcodec_alloc_context3(c);
    if (!cc) return result;
    if (m_impl->fmtCtx && m_impl->videoStreamIdx >= 0) {
        avcodec_parameters_to_context(cc, m_impl->fmtCtx->streams[m_impl->videoStreamIdx]->codecpar);
    }
    cc->thread_count = 1;
    cc->err_recognition = 0;
    if (avcodec_open2(cc, c, nullptr) < 0) {
        avcodec_free_context(&cc);
        return result;
    }

    auto isSlice = [&](const NALUnit &n) -> bool {
        if (n.codec == CodecType::H264) return n.nalType >= 1 && n.nalType <= 5;
        return n.nalType <= 21;
    };

    bool prevWasSlice = false;
    int dtsIdx = -1;
    int totalPkts = 0;

    std::vector<std::pair<int64_t, int>> pktPosList;

    for (auto &nal : nals) {
        if (isSlice(nal)) {
            if (!prevWasSlice) dtsIdx++;
            AVPacket *pkt = av_packet_alloc();
            pkt->data = (uint8_t *)nal.data.data();
            pkt->size = (int)nal.data.size();
            pkt->pos = (int64_t)nal.offset;
            pkt->pts = dtsIdx;
            pkt->dts = dtsIdx;
            pkt->flags = (nal.nalType == 5 || (nal.codec == CodecType::H265 && (nal.nalType >= 19 && nal.nalType <= 21)))
                             ? AV_PKT_FLAG_KEY : 0;
            pktPosList.push_back({nal.offset, dtsIdx});
            if (totalPkts < 10 || totalPkts == 0) {
                qDebug() << QString("[map] send pkt dtsIdx=%1 pos=%2 nalType=%3 pts=%4")
                                .arg(dtsIdx).arg(nal.offset).arg(nal.nalType).arg(pkt->pts);
            }
            avcodec_send_packet(cc, pkt);
            av_packet_unref(pkt);
            av_free(pkt);
            totalPkts++;
            prevWasSlice = true;
        } else {
            prevWasSlice = false;
        }
    }

    qDebug() << QString("[map] total pkts sent=%1 total unique dtsIdx=%2")
                    .arg(totalPkts).arg(dtsIdx + 1);

    avcodec_send_packet(cc, nullptr);

    std::unordered_map<int64_t, int> ptsToDisplay;
    AVFrame *frame = av_frame_alloc();
    int displayIdx = 0;
    while (avcodec_receive_frame(cc, frame) >= 0) {
        ptsToDisplay[frame->pts] = displayIdx;
        if (displayIdx < 10) {
            qDebug() << QString("[map] recv frame displayIdx=%1 frame.pts=%2 pict_type=%3")
                            .arg(displayIdx).arg(frame->pts).arg(frame->pict_type);
        }
        displayIdx++;
        av_frame_unref(frame);
    }
    qDebug() << QString("[map] total frames recv=%1").arg(displayIdx);
    av_frame_free(&frame);

    qDebug() << "[map] ptsToDisplay contents:";
    for (auto &[pts, didx] : ptsToDisplay) {
        if (didx < 10) {
            qDebug() << QString("  pts=%1 -> displayIdx=%2").arg(pts).arg(didx);
        }
    }

    int matched = 0;
    for (auto &[pos, didx] : pktPosList) {
        auto it = ptsToDisplay.find(didx);
        if (it != ptsToDisplay.end()) {
            result[pos] = it->second;
            matched++;
        }
    }
    qDebug() << QString("[map] matched %1 / %2 pkts").arg(matched).arg(pktPosList.size());

    if (!result.empty()) {
        auto it = result.begin();
        qDebug() << QString("[map] sample result: pos=%1 -> frameIdx=%2").arg(it->first).arg(it->second);
    }

    avcodec_free_context(&cc);
    return result;
}