#ifndef DIA_FFMPEG_DECODER_H_
#define DIA_FFMPEG_DECODER_H_

#include <QImage>
#include <string>
#include <unordered_map>
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

  bool OpenFile(const std::string& path, CodecType codec);
  void close();

  void reset();
  bool DecodeNextFrame(DecodedFrame& out);
  // convert == false only decodes timestamps (used while seeking).
  bool DecodeNextFrame(DecodedFrame& out, bool convert);
  bool SeekToFrame(int target_index);
  bool SeekByContainer(int target_index);

  bool DecodeKeyframes(std::vector<DecodedFrame>& out, int max_frames = 10);

  bool HasTimestamps() const;
  int FrameCount() const { return frame_count_; }
  int width() const { return width_; }
  int height() const { return height_; }
  double FrameRate() const { return frame_rate_; }
  int CurrentFrameIndex() const { return current_frame_; }
  const std::string& LastError() const { return error_; }

 private:
  bool ConvertFrame(const AVFrame* frame, DecodedFrame& out);

  struct Impl;
  Impl* impl_ = nullptr;
  int frame_count_ = 0;
  int width_ = 0;
  int height_ = 0;
  int current_frame_ = 0;
  double frame_rate_ = 30.0;
  std::string path_;
  CodecType codec_ = CodecType::kH264;
  std::string error_;
};

#endif  // DIA_FFMPEG_DECODER_H_
