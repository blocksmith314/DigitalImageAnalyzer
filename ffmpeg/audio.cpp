#include "audio.h"

#include "audiodecoder.h"

#include <cstring>

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QTimer>

namespace
{

    PcmFormat toPcmFormat(QAudioFormat::SampleFormat format)
    {
        switch (format)
        {
        case QAudioFormat::UInt8:
            return PcmFormat::U8;
        case QAudioFormat::Int16:
            return PcmFormat::S16;
        case QAudioFormat::Int32:
            return PcmFormat::S32;
        case QAudioFormat::Float:
            return PcmFormat::Float;
        default:
            return PcmFormat::S16;
        }
    }

} // namespace

// Pull-mode source: Qt's audio thread calls readData() at real-time pace, so
// the output can never run faster or slower than the declared format.
class AudioDevice : public QIODevice
{
public:
    explicit AudioDevice(AudioPlayer* player) : m_player(player) {}

    qint64 readData(char* data, qint64 maxLen) override { return m_player->readPcm(data, maxLen); }
    qint64 writeData(const char*, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return m_player->availablePcm() + QIODevice::bytesAvailable(); }

private:
    AudioPlayer* m_player;
};

struct AudioPlayer::Impl
{
    AudioDecoder* decoder = nullptr;
    QAudioFormat outFormat;
};

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout, this, &AudioPlayer::pump);
}

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open(const std::string& path)
{
    close();
    m_impl = new Impl();
    m_impl->decoder = new AudioDecoder();
    if (!m_impl->decoder->open(path) || !m_impl->decoder->hasAudio())
    {
        close();
        return false;
    }

    m_hasAudio = true;
    m_playing = false;
    m_eof = false;
    return true;
}

void AudioPlayer::close()
{
    m_timer->stop();
    m_playing = false;
    m_hasAudio = false;
    m_eof = false;
    if (m_sink)
    {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_device)
    {
        m_device->close();
        delete m_device;
        m_device = nullptr;
    }
    {
        QMutexLocker lock(&m_mutex);
        m_ring.clear();
    }
    if (m_impl)
    {
        delete m_impl->decoder;
        delete m_impl;
        m_impl = nullptr;
    }
}

void AudioPlayer::play()
{
    if (!m_hasAudio || !m_impl)
        return;

    if (!m_sink)
    {
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();

        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format))
            format = device.preferredFormat();

        m_impl->outFormat = format;
        m_impl->decoder->setOutputFormat(format.sampleRate(), format.channelCount(),
                                         toPcmFormat(format.sampleFormat()));
        m_sink = new QAudioSink(device, format, this);
        m_sink->setBufferSize(format.bytesForDuration(150000)); // 150 ms
        m_device = new AudioDevice(this);
        m_device->open(QIODevice::ReadOnly);
        m_sink->start(m_device);
    }
    else if (m_sink->state() != QAudio::ActiveState)
    {
        m_sink->resume();
    }

    m_playing = true;
    m_timer->start();
    pump();
}

void AudioPlayer::pause()
{
    m_timer->stop();
    if (m_sink)
        m_sink->suspend();
    m_playing = false;
}

void AudioPlayer::stop()
{
    pause();
    seekTo(0.0);
}

void AudioPlayer::seekTo(double seconds)
{
    if (!m_impl || !m_hasAudio)
        return;

    m_impl->decoder->seekTo(seconds);
    {
        QMutexLocker lock(&m_mutex);
        m_ring.clear();
    }
    m_eof = false;

    if (m_playing)
        pump();
}

qint64 AudioPlayer::availablePcm() const
{
    QMutexLocker lock(&m_mutex);
    return m_ring.size();
}

qint64 AudioPlayer::readPcm(char* data, qint64 maxLen)
{
    QMutexLocker lock(&m_mutex);
    const qint64 n = qMin<qint64>(maxLen, m_ring.size());
    if (n <= 0)
        return 0;
    memcpy(data, m_ring.constData(), (size_t)n);
    m_ring.remove(0, (int)n);
    return n;
}

void AudioPlayer::pump()
{
    if (!m_impl || !m_sink)
        return;

    const int maxRing = m_impl->outFormat.bytesForDuration(500000); // 500 ms
    QByteArray decoded;
    uint8_t buffer[16384];

    while (m_ring.size() + decoded.size() < maxRing)
    {
        const int n = m_impl->decoder->decode(buffer, (int)sizeof(buffer));
        if (n <= 0)
        {
            m_eof = true;
            break;
        }
        decoded.append((const char*)buffer, n);
    }

    if (!decoded.isEmpty())
    {
        QMutexLocker lock(&m_mutex);
        m_ring.append(decoded);
    }
}
