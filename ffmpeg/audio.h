#ifndef DIA_FFMPEG_AUDIO_H_
#define DIA_FFMPEG_AUDIO_H_

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <string>

class QAudioSink;
class QTimer;
class AudioDevice;

// Plays the audio track of the currently opened file through Qt Multimedia.
// Video playback is driven by MainWindow's own timer; this class decodes the
// selected audio stream on demand and keeps its own sink buffer filled.
class AudioPlayer : public QObject {
  Q_OBJECT
 public:
  explicit AudioPlayer(QObject* parent = nullptr);
  ~AudioPlayer() override;

  // Opens the first audio stream. Returns false when the file has no audio.
  bool open(const std::string& path);
  bool HasAudio() const { return has_audio_; }
  void close();

  void play();
  void pause();
  void stop();
  void SeekTo(double seconds);

 private:
  friend class AudioDevice;

  void pump();
  qint64 ReadPcm(char* data, qint64 maxLen);
  qint64 AvailablePcm() const;

  struct Impl;
  Impl* impl_ = nullptr;
  QAudioSink* sink_ = nullptr;
  AudioDevice* device_ = nullptr;
  QTimer* timer_ = nullptr;
  mutable QMutex mutex_;
  QByteArray ring_;
  bool has_audio_ = false;
  bool playing_ = false;
  bool eof_ = false;
};

#endif  // DIA_FFMPEG_AUDIO_H_
