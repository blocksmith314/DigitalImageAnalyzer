#include "audio.h"

#include <cstring>

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QTimer>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

namespace {

AVSampleFormat toAvSampleFormat(QAudioFormat::SampleFormat f) {
    switch (f) {
        case QAudioFormat::UInt8: return AV_SAMPLE_FMT_U8;
        case QAudioFormat::Int16: return AV_SAMPLE_FMT_S16;
        case QAudioFormat::Int32: return AV_SAMPLE_FMT_S32;
        case QAudioFormat::Float: return AV_SAMPLE_FMT_FLT;
        default: return AV_SAMPLE_FMT_S16;
    }
}

} // namespace

// Pull-mode source: Qt's audio thread calls readData() at real-time pace, so
// the output can never run faster or slower than the declared format.
class AudioDevice : public QIODevice {
public:
    explicit AudioDevice(AudioPlayer *player) : m_player(player) {}

    qint64 readData(char *data, qint64 maxLen) override {
        return m_player->readPcm(data, maxLen);
    }
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 bytesAvailable() const override {
        return m_player->availablePcm() + QIODevice::bytesAvailable();
    }

private:
    AudioPlayer *m_player;
};

struct AudioPlayer::Impl {
    AVFormatContext *fmt = nullptr;
    AVCodecContext *ctx = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    int streamIdx = -1;
    uint8_t *outBuf = nullptr;
    int outBufSize = 0;
    QAudioFormat outFormat;
};

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout, this, &AudioPlayer::pump);
}

AudioPlayer::~AudioPlayer() {
    close();
}

bool AudioPlayer::open(const std::string &path) {
    close();
    m_impl = new Impl();

    if (avformat_open_input(&m_impl->fmt, path.c_str(), nullptr, nullptr) < 0) {
        close();
        return false;
    }
    if (avformat_find_stream_info(m_impl->fmt, nullptr) < 0) {
        close();
        return false;
    }

    m_impl->streamIdx = -1;
    for (unsigned i = 0; i < m_impl->fmt->nb_streams; i++) {
        if (m_impl->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            m_impl->streamIdx = (int)i;
            break;
        }
    }
    if (m_impl->streamIdx < 0) {
        close();
        return false;
    }

    AVCodecParameters *par = m_impl->fmt->streams[m_impl->streamIdx]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        close();
        return false;
    }
    m_impl->ctx = avcodec_alloc_context3(dec);
    if (!m_impl->ctx || avcodec_parameters_to_context(m_impl->ctx, par) < 0 ||
        avcodec_open2(m_impl->ctx, dec, nullptr) < 0) {
        close();
        return false;
    }
    m_impl->pkt = av_packet_alloc();
    m_impl->frame = av_frame_alloc();
    if (!m_impl->pkt || !m_impl->frame) {
        close();
        return false;
    }

    m_hasAudio = true;
    m_playing = false;
    m_eof = false;
    return true;
}

void AudioPlayer::close() {
    m_timer->stop();
    m_playing = false;
    m_hasAudio = false;
    m_eof = false;
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_device) {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_ring.clear();
    }
    if (m_impl) {
        if (m_impl->swr) swr_free(&m_impl->swr);
        if (m_impl->outBuf) av_free(m_impl->outBuf);
        if (m_impl->frame) av_frame_free(&m_impl->frame);
        if (m_impl->pkt) av_packet_free(&m_impl->pkt);
        if (m_impl->ctx) avcodec_free_context(&m_impl->ctx);
        if (m_impl->fmt) avformat_close_input(&m_impl->fmt);
        delete m_impl;
        m_impl = nullptr;
    }
}

void AudioPlayer::play() {
    if (!m_hasAudio || !m_impl) return;

    if (!m_sink) {
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();

        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) format = device.preferredFormat();

        m_impl->outFormat = format;
        m_sink = new QAudioSink(device, format, this);
        m_sink->setBufferSize(format.bytesForDuration(150000)); // 150 ms
        m_device = new AudioDevice(this);
        m_device->open(QIODevice::ReadOnly);
        m_sink->start(m_device);
    } else if (m_sink->state() != QAudio::ActiveState) {
        m_sink->resume();
    }

    m_playing = true;
    m_timer->start();
    pump();
}

void AudioPlayer::pause() {
    m_timer->stop();
    if (m_sink) m_sink->suspend();
    m_playing = false;
}

void AudioPlayer::stop() {
    pause();
    seekTo(0.0);
}

void AudioPlayer::seekTo(double seconds) {
    if (!m_impl || !m_hasAudio) return;

    AVStream *st = m_impl->fmt->streams[m_impl->streamIdx];
    const int64_t ts = (int64_t)(seconds / av_q2d(st->time_base));
    if (av_seek_frame(m_impl->fmt, m_impl->streamIdx, ts, AVSEEK_FLAG_BACKWARD) < 0) return;

    avcodec_flush_buffers(m_impl->ctx);
    if (m_impl->swr) swr_convert(m_impl->swr, nullptr, 0, nullptr, 0);
    {
        QMutexLocker lock(&m_mutex);
        m_ring.clear();
    }
    m_eof = false;

    if (m_playing) pump();
}

qint64 AudioPlayer::availablePcm() const {
    QMutexLocker lock(&m_mutex);
    return m_ring.size();
}

qint64 AudioPlayer::readPcm(char *data, qint64 maxLen) {
    QMutexLocker lock(&m_mutex);
    const qint64 n = qMin<qint64>(maxLen, m_ring.size());
    if (n <= 0) return 0;
    memcpy(data, m_ring.constData(), (size_t)n);
    m_ring.remove(0, (int)n);
    return n;
}

void AudioPlayer::pump() {
    if (!m_impl || !m_sink) return;

    const int maxRing = m_impl->outFormat.bytesForDuration(500000); // 500 ms
    QByteArray decoded;

    while (!m_eof && m_ring.size() + decoded.size() < maxRing) {
        if (av_read_frame(m_impl->fmt, m_impl->pkt) < 0) {
            m_eof = true;
            break;
        }
        if (m_impl->pkt->stream_index != m_impl->streamIdx) {
            av_packet_unref(m_impl->pkt);
            continue;
        }
        if (avcodec_send_packet(m_impl->ctx, m_impl->pkt) < 0) {
            av_packet_unref(m_impl->pkt);
            continue;
        }
        av_packet_unref(m_impl->pkt);

        while (avcodec_receive_frame(m_impl->ctx, m_impl->frame) >= 0) {
            if (!m_impl->swr) {
                AVChannelLayout outLayout;
                av_channel_layout_default(&outLayout, m_impl->outFormat.channelCount());

                AVChannelLayout inLayout = m_impl->frame->ch_layout;
                if (inLayout.nb_channels <= 0)
                    av_channel_layout_copy(&inLayout, &m_impl->ctx->ch_layout);
                const int inRate = m_impl->frame->sample_rate > 0
                                       ? m_impl->frame->sample_rate
                                       : m_impl->ctx->sample_rate;
                const AVSampleFormat inFmt = m_impl->frame->format >= 0
                                                 ? (AVSampleFormat)m_impl->frame->format
                                                 : m_impl->ctx->sample_fmt;
                swr_alloc_set_opts2(&m_impl->swr, &outLayout,
                                    toAvSampleFormat(m_impl->outFormat.sampleFormat()),
                                    m_impl->outFormat.sampleRate(),
                                    &inLayout, inFmt, inRate, 0, nullptr);
                if (!m_impl->swr || swr_init(m_impl->swr) < 0) {
                    if (m_impl->swr) swr_free(&m_impl->swr);
                    m_eof = true;
                    av_frame_unref(m_impl->frame);
                    break;
                }
            }

            const int outSamples = swr_get_out_samples(m_impl->swr, m_impl->frame->nb_samples);
            if (outSamples <= 0) {
                av_frame_unref(m_impl->frame);
                continue;
            }
            const int bytesPerSample = m_impl->outFormat.bytesPerSample();
            const int channels = m_impl->outFormat.channelCount();
            const int maxBytes = outSamples * channels * bytesPerSample;
            if (maxBytes > m_impl->outBufSize) {
                if (m_impl->outBuf) av_free(m_impl->outBuf);
                m_impl->outBuf = (uint8_t *)av_malloc((size_t)maxBytes);
                m_impl->outBufSize = m_impl->outBuf ? maxBytes : 0;
            }
            if (m_impl->outBuf) {
                const int converted = swr_convert(
                    m_impl->swr, &m_impl->outBuf, outSamples,
                    (const uint8_t **)m_impl->frame->extended_data, m_impl->frame->nb_samples);
                if (converted > 0) {
                    decoded.append((const char *)m_impl->outBuf,
                                   converted * channels * bytesPerSample);
                }
            }
            av_frame_unref(m_impl->frame);
        }
    }

    if (!decoded.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_ring.append(decoded);
    }
}
