#include "audio.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutexLocker>
#include <QTimer>
#include <cstring>

#include "audiodecoder.h"

namespace {

PcmFormat toPcmFormat(QAudioFormat::SampleFormat format) {
  switch (format) {
    case QAudioFormat::UInt8:
      return PcmFormat::kU8;
    case QAudioFormat::Int16:
      return PcmFormat::kS16;
    case QAudioFormat::Int32:
      return PcmFormat::kS32;
    case QAudioFormat::Float:
      return PcmFormat::kFloat;
    default:
      return PcmFormat::kS16;
  }
}

}  // namespace

// Pull-mode source: Qt's audio thread calls readData() at real-time pace, so
// the output can never run faster or slower than the declared format.
class AudioDevice : public QIODevice {
 public:
  explicit AudioDevice(AudioPlayer* player) : player_(player) {}

  qint64 readData(char* data, qint64 maxLen) override {
    return player_->ReadPcm(data, maxLen);
  }
  qint64 writeData(const char*, qint64) override { return -1; }
  qint64 bytesAvailable() const override {
    return player_->AvailablePcm() + QIODevice::bytesAvailable();
  }

 private:
  AudioPlayer* player_;
};

struct AudioPlayer::Impl {
  AudioDecoder* decoder = nullptr;
  QAudioFormat outFormat;
};

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent) {
  timer_ = new QTimer(this);
  timer_->setInterval(20);
  connect(timer_, &QTimer::timeout, this, &AudioPlayer::pump);
}

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open(const std::string& path) {
  close();
  impl_ = new Impl();
  impl_->decoder = new AudioDecoder();
  if (!impl_->decoder->open(path) || !impl_->decoder->HasAudio()) {
    close();
    return false;
  }

  has_audio_ = true;
  playing_ = false;
  eof_ = false;
  return true;
}

void AudioPlayer::close() {
  timer_->stop();
  playing_ = false;
  has_audio_ = false;
  eof_ = false;
  if (sink_) {
    sink_->stop();
    delete sink_;
    sink_ = nullptr;
  }
  if (device_) {
    device_->close();
    delete device_;
    device_ = nullptr;
  }
  {
    QMutexLocker lock(&mutex_);
    ring_.clear();
  }
  if (impl_) {
    delete impl_->decoder;
    delete impl_;
    impl_ = nullptr;
  }
}

void AudioPlayer::play() {
  if (!has_audio_ || !impl_) return;

  if (!sink_) {
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) format = device.preferredFormat();

    impl_->outFormat = format;
    impl_->decoder->SetOutputFormat(format.sampleRate(),
                                      format.channelCount(),
                                      toPcmFormat(format.sampleFormat()));
    sink_ = new QAudioSink(device, format, this);
    sink_->setBufferSize(format.bytesForDuration(150000));  // 150 ms
    device_ = new AudioDevice(this);
    device_->open(QIODevice::ReadOnly);
    sink_->start(device_);
  } else if (sink_->state() != QAudio::ActiveState) {
    sink_->resume();
  }

  playing_ = true;
  timer_->start();
  pump();
}

void AudioPlayer::pause() {
  timer_->stop();
  if (sink_) sink_->suspend();
  playing_ = false;
}

void AudioPlayer::stop() {
  pause();
  SeekTo(0.0);
}

void AudioPlayer::SeekTo(double seconds) {
  if (!impl_ || !has_audio_) return;

  impl_->decoder->SeekTo(seconds);
  {
    QMutexLocker lock(&mutex_);
    ring_.clear();
  }
  eof_ = false;

  if (playing_) pump();
}

qint64 AudioPlayer::AvailablePcm() const {
  QMutexLocker lock(&mutex_);
  return ring_.size();
}

qint64 AudioPlayer::ReadPcm(char* data, qint64 maxLen) {
  QMutexLocker lock(&mutex_);
  const qint64 n = qMin<qint64>(maxLen, ring_.size());
  if (n <= 0) return 0;
  memcpy(data, ring_.constData(), (size_t)n);
  ring_.remove(0, (int)n);
  return n;
}

void AudioPlayer::pump() {
  if (!impl_ || !sink_) return;

  const int max_ring = impl_->outFormat.bytesForDuration(500000);  // 500 ms
  QByteArray decoded;
  uint8_t buffer[16384];

  while (ring_.size() + decoded.size() < max_ring) {
    const int n = impl_->decoder->decode(buffer, (int)sizeof(buffer));
    if (n <= 0) {
      eof_ = true;
      break;
    }
    decoded.append((const char*)buffer, n);
  }

  if (!decoded.isEmpty()) {
    QMutexLocker lock(&mutex_);
    ring_.append(decoded);
  }
}
