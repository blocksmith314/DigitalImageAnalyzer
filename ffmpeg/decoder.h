#ifndef DIA_FFMPEG_DECODER_H_
#define DIA_FFMPEG_DECODER_H_

#include <QImage>
#include <string>
#include <vector>

#include "nalunit.h"

struct AVFrame;

struct DecodedFrame {
  int64_t pts = 0;
  double pts_sec = 0.0;  // presentation time in seconds
  int width = 0;
  int height = 0;
  QImage image;
};

class FFmpegDecoder {
 public:
  FFmpegDecoder();
  ~FFmpegDecoder();

  FFmpegDecoder(const FFmpegDecoder&) = delete;
  FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;
  FFmpegDecoder(FFmpegDecoder&&) = delete;
  FFmpegDecoder& operator=(FFmpegDecoder&&) = delete;

  bool OpenFile(const std::string& path, CodecType codec);
  void close();

  void reset();
  bool DecodeNextFrame(DecodedFrame& out);
  // convert == false only decodes timestamps (used while seeking).
  bool DecodeNextFrame(DecodedFrame& out, bool convert);
  bool SeekToFrame(int target_index);
  bool SeekByContainer(int target_index);

  bool DecodeKeyframes(std::vector<DecodedFrame>& out, int max_frames = 10);

  struct SeekStats {
    bool used_container = false;
    int frames_decoded = 0;
    qint64 seek_io_ms = 0;
    qint64 decode_ms = 0;
  };

  const SeekStats& LastSeekStats() const { return last_seek_stats_; }

  bool HasTimestamps() const;
  int FrameCount() const { return frame_count_; }
  int width() const { return width_; }
  int height() const { return height_; }
  double FrameRate() const { return frame_rate_; }
  double DurationSec() const { return duration_sec_; }
  int CurrentFrameIndex() const { return current_frame_; }
  const std::string& LastError() const { return error_; }

 private:
  bool ConvertFrame(const AVFrame* frame, DecodedFrame& out);
  bool BuildFrameIndex();

  struct Impl;
  Impl* impl_ = nullptr;
  int frame_count_ = 0;
  int width_ = 0;
  int height_ = 0;
  int current_frame_ = 0;
  SeekStats last_seek_stats_;
  double frame_rate_ = 30.0;
  double duration_sec_ = 0.0;
  std::string path_;
  CodecType codec_ = CodecType::kH264;
  std::string error_;
};

#endif  // DIA_FFMPEG_DECODER_H_
