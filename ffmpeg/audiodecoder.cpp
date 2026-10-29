#include "audiodecoder.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace
{

    AVSampleFormat toAv(PcmFormat format)
    {
        switch (format)
        {
        case PcmFormat::U8:
            return AV_SAMPLE_FMT_U8;
        case PcmFormat::S16:
            return AV_SAMPLE_FMT_S16;
        case PcmFormat::S32:
            return AV_SAMPLE_FMT_S32;
        case PcmFormat::Float:
            return AV_SAMPLE_FMT_FLT;
        }
        return AV_SAMPLE_FMT_S16;
    }

} // namespace

struct AudioDecoder::Impl
{
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;

    int streamIdx = -1;
    int sourceRate = 0;
    int sourceChannels = 0;

    int outRate = 48000;
    int outChannels = 2;
    AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;

    bool eof = false;
    bool flushed = false;

    void initResampler();
};

void AudioDecoder::Impl::initResampler()
{
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, outChannels);

    AVChannelLayout inLayout = frame->ch_layout;
    if (inLayout.nb_channels <= 0)
        av_channel_layout_copy(&inLayout, &ctx->ch_layout);
    const int inRate = frame->sample_rate > 0 ? frame->sample_rate : ctx->sample_rate;
    const AVSampleFormat inFmt = frame->format >= 0 ? static_cast<AVSampleFormat>(frame->format) : ctx->sample_fmt;

    swr_alloc_set_opts2(&swr, &outLayout, outFormat, outRate, &inLayout, inFmt, inRate, 0, nullptr);
    if (swr)
        swr_init(swr);
}

AudioDecoder::AudioDecoder() : m_impl(new Impl()) {}

AudioDecoder::~AudioDecoder()
{
    close();
    delete m_impl;
}

bool AudioDecoder::open(const std::string& path)
{
    close();
    if (!m_impl)
        m_impl = new Impl();

    if (avformat_open_input(&m_impl->fmt, path.c_str(), nullptr, nullptr) < 0)
    {
        close();
        return false;
    }
    if (avformat_find_stream_info(m_impl->fmt, nullptr) < 0)
    {
        close();
        return false;
    }

    m_impl->streamIdx = -1;
    for (unsigned i = 0; i < m_impl->fmt->nb_streams; i++)
    {
        if (m_impl->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_impl->streamIdx = static_cast<int>(i);
            break;
        }
    }
    if (m_impl->streamIdx < 0)
    {
        close();
        return false;
    }

    AVCodecParameters* par = m_impl->fmt->streams[m_impl->streamIdx]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
    if (!decoder)
    {
        close();
        return false;
    }
    m_impl->ctx = avcodec_alloc_context3(decoder);
    if (!m_impl->ctx || avcodec_parameters_to_context(m_impl->ctx, par) < 0 ||
        avcodec_open2(m_impl->ctx, decoder, nullptr) < 0)
    {
        close();
        return false;
    }
    m_impl->pkt = av_packet_alloc();
    m_impl->frame = av_frame_alloc();
    if (!m_impl->pkt || !m_impl->frame)
    {
        close();
        return false;
    }

    m_impl->sourceRate = m_impl->ctx->sample_rate;
    m_impl->sourceChannels = m_impl->ctx->ch_layout.nb_channels;
    m_impl->eof = false;
    m_impl->flushed = false;
    return true;
}

void AudioDecoder::close()
{
    if (!m_impl)
        return;
    if (m_impl->swr)
        swr_free(&m_impl->swr);
    if (m_impl->frame)
        av_frame_free(&m_impl->frame);
    if (m_impl->pkt)
        av_packet_free(&m_impl->pkt);
    if (m_impl->ctx)
        avcodec_free_context(&m_impl->ctx);
    if (m_impl->fmt)
        avformat_close_input(&m_impl->fmt);
    *m_impl = Impl();
}

bool AudioDecoder::isOpen() const { return m_impl && m_impl->ctx != nullptr; }

bool AudioDecoder::hasAudio() const { return isOpen(); }

int AudioDecoder::sourceSampleRate() const { return m_impl ? m_impl->sourceRate : 0; }

int AudioDecoder::sourceChannels() const { return m_impl ? m_impl->sourceChannels : 0; }

void AudioDecoder::setOutputFormat(int sampleRate, int channels, PcmFormat format)
{
    if (!m_impl)
        return;
    m_impl->outRate = sampleRate > 0 ? sampleRate : 48000;
    m_impl->outChannels = channels > 0 ? channels : 2;
    m_impl->outFormat = toAv(format);
    if (m_impl->swr)
        swr_free(&m_impl->swr);
}

int AudioDecoder::outputSampleRate() const { return m_impl ? m_impl->outRate : 0; }

int AudioDecoder::outputChannels() const { return m_impl ? m_impl->outChannels : 0; }

void AudioDecoder::seekTo(double seconds)
{
    if (!isOpen())
        return;
    AVStream* stream = m_impl->fmt->streams[m_impl->streamIdx];
    const int64_t timestamp = static_cast<int64_t>(seconds / av_q2d(stream->time_base));
    if (av_seek_frame(m_impl->fmt, m_impl->streamIdx, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
    {
        return;
    }
    avcodec_flush_buffers(m_impl->ctx);
    if (m_impl->swr)
        swr_convert(m_impl->swr, nullptr, 0, nullptr, 0);
    m_impl->eof = false;
    m_impl->flushed = false;
}


int AudioDecoder::decode(uint8_t* out, int maxBytes)
{
    if (!isOpen() || !out || maxBytes <= 0)
        return 0;

    const int bytesPerFrame = m_impl->outChannels * av_get_bytes_per_sample(m_impl->outFormat);
    if (bytesPerFrame <= 0)
        return 0;

    int written = 0;
    while (written + bytesPerFrame <= maxBytes)
    {
        // Drain samples buffered by the resampler first.
        if (m_impl->swr)
        {
            const int buffered = swr_get_out_samples(m_impl->swr, 0);
            if (buffered > 0)
            {
                const int maxFrames = (maxBytes - written) / bytesPerFrame;
                uint8_t* dst = out + written;
                const int converted = swr_convert(m_impl->swr, &dst, maxFrames, nullptr, 0);
                if (converted <= 0)
                    break;
                written += converted * bytesPerFrame;
                continue;
            }
        }

        const int ret = avcodec_receive_frame(m_impl->ctx, m_impl->frame);
        if (ret == AVERROR(EAGAIN))
        {
            if (m_impl->eof)
            {
                if (!m_impl->flushed)
                {
                    avcodec_send_packet(m_impl->ctx, nullptr);
                    m_impl->flushed = true;
                    continue;
                }
                break;
            }
            if (av_read_frame(m_impl->fmt, m_impl->pkt) < 0)
            {
                m_impl->eof = true;
                continue;
            }
            if (m_impl->pkt->stream_index != m_impl->streamIdx)
            {
                av_packet_unref(m_impl->pkt);
                continue;
            }
            avcodec_send_packet(m_impl->ctx, m_impl->pkt);
            av_packet_unref(m_impl->pkt);
            continue;
        }
        if (ret < 0)
            break;

        if (!m_impl->swr)
            m_impl->initResampler();
        const int maxFrames = (maxBytes - written) / bytesPerFrame;
        uint8_t* dst = out + written;
        const int converted =
            swr_convert(m_impl->swr, &dst, maxFrames, const_cast<const uint8_t**>(m_impl->frame->extended_data),
                        m_impl->frame->nb_samples);
        av_frame_unref(m_impl->frame);
        if (converted <= 0)
            break;
        written += converted * bytesPerFrame;
    }
    return written;
}

bool AudioDecoder::atEnd() const { return m_impl && m_impl->eof; }
