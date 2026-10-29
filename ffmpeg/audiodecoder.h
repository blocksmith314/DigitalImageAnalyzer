#ifndef DIA_FFMPEG_AUDIODECODER_H_
#define DIA_FFMPEG_AUDIODECODER_H_

#include <cstdint>
#include <string>

// PCM sample formats accepted by AudioDecoder::SetOutputFormat().
enum class PcmFormat { kU8, kS16, kS32, kFloat };

// Decodes the first audio stream of a media file to interleaved PCM.
// This class has no Qt Multimedia dependency so it can be unit tested
// without an audio device; AudioPlayer wraps it for playback.
class AudioDecoder {
 public:
  AudioDecoder();
  ~AudioDecoder();

  AudioDecoder(const AudioDecoder&) = delete;
  AudioDecoder& operator=(const AudioDecoder&) = delete;

  bool open(const std::string& path);
  void close();
  bool IsOpen() const;
  bool HasAudio() const;

  int SourceSampleRate() const;
  int SourceChannels() const;

  void SetOutputFormat(int sample_rate, int channels, PcmFormat format);
  int OutputSampleRate() const;
  int OutputChannels() const;

  void SeekTo(double seconds);

  // Decodes up to max_bytes of interleaved PCM into out.
  // Returns the number of bytes written; 0 means end of stream.
  int decode(uint8_t* out, int max_bytes);
  bool AtEnd() const;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

#endif  // DIA_FFMPEG_AUDIODECODER_H_
