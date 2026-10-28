#pragma once

#include <cstdint>
#include <string>

// PCM sample formats accepted by AudioDecoder::setOutputFormat().
enum class PcmFormat { U8, S16, S32, Float };

// Decodes the first audio stream of a media file to interleaved PCM.
// This class has no Qt Multimedia dependency so it can be unit tested
// without an audio device; AudioPlayer wraps it for playback.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    bool open(const std::string &path);
    void close();
    bool isOpen() const;
    bool hasAudio() const;

    int sourceSampleRate() const;
    int sourceChannels() const;

    void setOutputFormat(int sampleRate, int channels, PcmFormat format);
    int outputSampleRate() const;
    int outputChannels() const;

    void seekTo(double seconds);

    // Decodes up to maxBytes of interleaved PCM into out.
    // Returns the number of bytes written; 0 means end of stream.
    int decode(uint8_t *out, int maxBytes);
    bool atEnd() const;

private:
    struct Impl;
    Impl *m_impl = nullptr;
};
