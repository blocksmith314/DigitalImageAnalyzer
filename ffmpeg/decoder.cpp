#include "decoder.h"
#include <QImage>

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