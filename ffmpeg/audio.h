#pragma once

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
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    // Opens the first audio stream. Returns false when the file has no audio.
    bool open(const std::string &path);
    bool hasAudio() const { return m_hasAudio; }
    void close();

    void play();
    void pause();
    void stop();
    void seekTo(double seconds);

private:
    friend class AudioDevice;

    void pump();
    qint64 readPcm(char *data, qint64 maxLen);
    qint64 availablePcm() const;

    struct Impl;
    Impl *m_impl = nullptr;
    QAudioSink *m_sink = nullptr;
    AudioDevice *m_device = nullptr;
    QTimer *m_timer = nullptr;
    mutable QMutex m_mutex;
    QByteArray m_ring;
    bool m_hasAudio = false;
    bool m_playing = false;
    bool m_eof = false;
};
