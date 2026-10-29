#include "decoder.h"

#include <QDebug>
#include <QImage>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

struct FFmpegDecoder::Impl {
  AVFormatContext* fmtCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  SwsContext* sws_ctx = nullptr;
  uint8_t* rgbBuffer = nullptr;
  int rgb_buffer_size = 0;
  int video_stream_idx = -1;
  bool is_raw = false;       // raw H.264/H.265 (no container timestamps)
  bool has_pending = false;  // one frame decoded by SeekByContainer
  DecodedFrame pending;
  int pending_index = 0;
  AVFrame* last_frame = nullptr;  // reference to the last decoded frame
};

FFmpegDecoder::FFmpegDecoder() : impl_(new Impl()) {}

FFmpegDecoder::~FFmpegDecoder() {
  close();
  delete impl_;
}

bool FFmpegDecoder::OpenFile(const std::string& path, CodecType codec) {
  close();
  if (impl_) *impl_ = Impl();

  int codec_id =
      (codec == CodecType::kH264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;

  // Let FFmpeg auto-detect the input: raw H.264/H.265 streams as well as
  // containers (MP4/MOV/MKV/TS...).
  if (avformat_open_input(&impl_->fmtCtx, path.c_str(), nullptr, nullptr) !=
      0) {
    error_ = "Failed to open input";
    close();
    return false;
  }

  if (avformat_find_stream_info(impl_->fmtCtx, nullptr) < 0) {
    error_ = "Failed to find stream info";
    close();
    return false;
  }

  impl_->video_stream_idx = -1;
  for (unsigned int i = 0; i < impl_->fmtCtx->nb_streams; i++) {
    if (impl_->fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      impl_->video_stream_idx = i;
      break;
    }
  }

  if (impl_->video_stream_idx < 0) {
    error_ = "No video stream found";
    close();
    return false;
  }

  AVStream* stream = impl_->fmtCtx->streams[impl_->video_stream_idx];
  const AVCodec* codecPtr = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codecPtr) codecPtr = avcodec_find_decoder((AVCodecID)codec_id);
  if (!codecPtr) {
    error_ = "Codec not found";
    close();
    return false;
  }

  impl_->codecCtx = avcodec_alloc_context3(codecPtr);
  if (!impl_->codecCtx) {
    error_ = "Failed to alloc codec ctx";
    close();
    return false;
  }

  if (avcodec_parameters_to_context(impl_->codecCtx, stream->codecpar) < 0) {
    error_ = "Failed to copy codec params";
    close();
    return false;
  }

  if (avcodec_open2(impl_->codecCtx, codecPtr, nullptr) < 0) {
    error_ = "Failed to open codec";
    close();
    return false;
  }

  width_ = impl_->codecCtx->width;
  height_ = impl_->codecCtx->height;

  impl_->rgb_buffer_size =
      av_image_get_buffer_size(AV_PIX_FMT_RGB24, width_, height_, 1);
  impl_->rgbBuffer = (uint8_t*)av_malloc(impl_->rgb_buffer_size);

  if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
    frame_rate_ = av_q2d(stream->avg_frame_rate);
  } else if (stream->r_frame_rate.den && stream->r_frame_rate.num) {
    frame_rate_ = av_q2d(stream->r_frame_rate);
  }
  if (frame_rate_ <= 0) frame_rate_ = 30.0;

  const char* fmtName = (impl_->fmtCtx->iformat && impl_->fmtCtx->iformat->name)
                            ? impl_->fmtCtx->iformat->name
                            : "";
  impl_->is_raw =
      (std::strcmp(fmtName, "h264") == 0 || std::strcmp(fmtName, "hevc") == 0);

  path_ = path;
  codec_ = codec;
  frame_count_ = 0;
  current_frame_ = 0;
  return true;
}

bool FFmpegDecoder::HasTimestamps() const { return impl_ && !impl_->is_raw; }

void FFmpegDecoder::close() {
  if (impl_) {
    if (impl_->sws_ctx) {
      sws_freeContext(impl_->sws_ctx);
      impl_->sws_ctx = nullptr;
    }
    if (impl_->rgbBuffer) {
      av_free(impl_->rgbBuffer);
      impl_->rgbBuffer = nullptr;
    }
    if (impl_->last_frame) {
      av_frame_free(&impl_->last_frame);
    }
    if (impl_->codecCtx) {
      avcodec_free_context(&impl_->codecCtx);
      impl_->codecCtx = nullptr;
    }
    if (impl_->fmtCtx) {
      avformat_close_input(&impl_->fmtCtx);
      impl_->fmtCtx = nullptr;
    }
  }
}

bool FFmpegDecoder::DecodeNextFrame(DecodedFrame& out) {
  return DecodeNextFrame(out, true);
}

bool FFmpegDecoder::ConvertFrame(const AVFrame* frame, DecodedFrame& out) {
  if (!frame || !impl_ || !impl_->codecCtx) return false;
  if (!impl_->sws_ctx) {
    impl_->sws_ctx = sws_getContext(width_, height_, impl_->codecCtx->pix_fmt,
                                    width_, height_, AV_PIX_FMT_RGB24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!impl_->sws_ctx) return false;
  }

  uint8_t* dst[1] = {impl_->rgbBuffer};
  int dst_linesize[1] = {width_ * 3};
  sws_scale(impl_->sws_ctx, frame->data, frame->linesize, 0, height_, dst,
            dst_linesize);

  QImage img(impl_->rgbBuffer, width_, height_, QImage::Format_RGB888);
  out.width = width_;
  out.height = height_;
  out.image = img.copy();
  return true;
}

bool FFmpegDecoder::DecodeNextFrame(DecodedFrame& out, bool convert) {
  if (!impl_ || !impl_->codecCtx || !impl_->fmtCtx) return false;

  if (impl_->has_pending) {
    out = impl_->pending;
    impl_->has_pending = false;
    current_frame_ = impl_->pending_index + 1;
    return true;
  }

  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  bool got_frame = false;

  while (av_read_frame(impl_->fmtCtx, pkt) >= 0) {
    if (pkt->stream_index != impl_->video_stream_idx) {
      av_packet_unref(pkt);
      continue;
    }

    if (avcodec_send_packet(impl_->codecCtx, pkt) < 0) {
      av_packet_unref(pkt);
      continue;
    }

    while (avcodec_receive_frame(impl_->codecCtx, frame) >= 0) {
      // Keep a reference to the frame so it can be converted on demand
      // (seeking decodes many frames and only shows the last one).
      if (!impl_->last_frame) impl_->last_frame = av_frame_alloc();
      av_frame_unref(impl_->last_frame);
      av_frame_ref(impl_->last_frame, frame);

      out.pts = frame->pts;
      out.pts_sec =
          (frame->pts != AV_NOPTS_VALUE)
              ? frame->pts *
                    av_q2d(impl_->fmtCtx->streams[impl_->video_stream_idx]
                               ->time_base)
              : 0.0;
      out.width = width_;
      out.height = height_;
      if (convert) ConvertFrame(frame, out);

      frame_count_++;
      current_frame_++;
      got_frame = true;

      av_packet_unref(pkt);
      av_frame_unref(frame);
      av_packet_free(&pkt);
      av_frame_free(&frame);
      return true;
    }
    av_packet_unref(pkt);
  }

  // flush
  avcodec_send_packet(impl_->codecCtx, nullptr);
  if (avcodec_receive_frame(impl_->codecCtx, frame) >= 0) {
    if (!impl_->sws_ctx) {
      impl_->sws_ctx = sws_getContext(width_, height_, impl_->codecCtx->pix_fmt,
                                      width_, height_, AV_PIX_FMT_RGB24,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    uint8_t* dst[1] = {impl_->rgbBuffer};
    int dst_linesize[1] = {width_ * 3};
    sws_scale(impl_->sws_ctx, frame->data, frame->linesize, 0, height_, dst,
              dst_linesize);
    QImage img(impl_->rgbBuffer, width_, height_, QImage::Format_RGB888);
    out.pts = frame->pts;
    out.pts_sec =
        (frame->pts != AV_NOPTS_VALUE)
            ? frame->pts *
                  av_q2d(impl_->fmtCtx->streams[impl_->video_stream_idx]
                             ->time_base)
            : 0.0;
    out.width = width_;
    out.height = height_;
    out.image = img.copy();
    frame_count_++;
    current_frame_++;
    got_frame = true;
  }

  av_frame_free(&frame);
  av_packet_free(&pkt);
  return got_frame;
}

bool FFmpegDecoder::DecodeKeyframes(std::vector<DecodedFrame>& out,
                                     int max_frames) {
  if (!impl_ || !impl_->codecCtx || !impl_->fmtCtx) return false;

  int decoded = 0;
  DecodedFrame frame;
  while (decoded < max_frames && DecodeNextFrame(frame)) {
    out.push_back(std::move(frame));
    decoded++;
  }
  return decoded > 0;
}

void FFmpegDecoder::reset() {
  if (!path_.empty()) {
    OpenFile(path_, codec_);
  }
}

bool FFmpegDecoder::SeekToFrame(int target_index) {
  if (!impl_ || !impl_->codecCtx || !impl_->fmtCtx) return false;
  if (target_index <= 0) {
    reset();
    current_frame_ = 0;
    return true;
  }
  if (target_index == current_frame_) return true;

  bool seek_attempted = false;
  if (!impl_->is_raw &&
      (target_index < current_frame_ || target_index > current_frame_ + 1)) {
    seek_attempted = true;
    if (SeekByContainer(target_index)) return true;
  }

  if (seek_attempted || target_index < current_frame_) {
    reset();
    current_frame_ = 0;
  }
  DecodedFrame dummy;
  while (current_frame_ < target_index) {
    if (!DecodeNextFrame(dummy, false)) return false;
  }
  return true;
}

bool FFmpegDecoder::SeekByContainer(int target_index) {
  if (!impl_ || !impl_->fmtCtx || impl_->video_stream_idx < 0 ||
      frame_rate_ <= 0.0)
    return false;
  AVStream* st = impl_->fmtCtx->streams[impl_->video_stream_idx];
  if (!st || st->time_base.num <= 0 || st->time_base.den <= 0) return false;

  const int64_t target_pts =
      (int64_t)std::llround((double)target_index * st->time_base.den /
                            (frame_rate_ * st->time_base.num));

  const int seek_ret = av_seek_frame(impl_->fmtCtx, impl_->video_stream_idx,
                                     target_pts, AVSEEK_FLAG_BACKWARD);
  if (seek_ret < 0) return false;

  avcodec_flush_buffers(impl_->codecCtx);
  // Drop any frames that were decoded but not consumed before the seek.
  AVFrame* stale = av_frame_alloc();
  while (avcodec_receive_frame(impl_->codecCtx, stale) >= 0)
    av_frame_unref(stale);
  av_frame_free(&stale);
  current_frame_ = 0;

  DecodedFrame f;
  while (DecodeNextFrame(f, false)) {
    if (f.pts == AV_NOPTS_VALUE) return false;  // no container timestamps
    if (f.pts >= target_pts) {
      ConvertFrame(impl_->last_frame, f);
      impl_->pending = f;
      impl_->pending_index = target_index;
      impl_->has_pending = true;
      current_frame_ = target_index;
      return true;
    }
  }
  return false;
}
