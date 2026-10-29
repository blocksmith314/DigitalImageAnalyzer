#include "audiodecoder.h"

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace {

AVSampleFormat toAv(PcmFormat format) {
  switch (format) {
    case PcmFormat::kU8:
      return AV_SAMPLE_FMT_U8;
    case PcmFormat::kS16:
      return AV_SAMPLE_FMT_S16;
    case PcmFormat::kS32:
      return AV_SAMPLE_FMT_S32;
    case PcmFormat::kFloat:
      return AV_SAMPLE_FMT_FLT;
  }
  return AV_SAMPLE_FMT_S16;
}

}  // namespace

struct AudioDecoder::Impl {
  AVFormatContext* fmt = nullptr;
  AVCodecContext* ctx = nullptr;
  SwrContext* swr = nullptr;
  AVPacket* pkt = nullptr;
  AVFrame* frame = nullptr;

  int stream_idx = -1;
  int source_rate = 0;
  int SourceChannels = 0;

  int out_rate = 48000;
  int out_channels = 2;
  AVSampleFormat outFormat = AV_SAMPLE_FMT_S16;

  bool eof = false;
  bool flushed = false;

  void initResampler();
};

void AudioDecoder::Impl::initResampler() {
  AVChannelLayout outLayout;
  av_channel_layout_default(&outLayout, out_channels);

  AVChannelLayout inLayout = frame->ch_layout;
  if (inLayout.nb_channels <= 0)
    av_channel_layout_copy(&inLayout, &ctx->ch_layout);
  const int in_rate =
      frame->sample_rate > 0 ? frame->sample_rate : ctx->sample_rate;
  const AVSampleFormat inFmt = frame->format >= 0
                                   ? static_cast<AVSampleFormat>(frame->format)
                                   : ctx->sample_fmt;

  swr_alloc_set_opts2(&swr, &outLayout, outFormat, out_rate, &inLayout, inFmt,
                      in_rate, 0, nullptr);
  if (swr) swr_init(swr);
}

AudioDecoder::AudioDecoder() : impl_(new Impl()) {}

AudioDecoder::~AudioDecoder() {
  close();
  delete impl_;
}

bool AudioDecoder::open(const std::string& path) {
  close();
  if (!impl_) impl_ = new Impl();

  if (avformat_open_input(&impl_->fmt, path.c_str(), nullptr, nullptr) < 0) {
    close();
    return false;
  }
  if (avformat_find_stream_info(impl_->fmt, nullptr) < 0) {
    close();
    return false;
  }

  impl_->stream_idx = -1;
  for (unsigned i = 0; i < impl_->fmt->nb_streams; i++) {
    if (impl_->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      impl_->stream_idx = static_cast<int>(i);
      break;
    }
  }
  if (impl_->stream_idx < 0) {
    close();
    return false;
  }

  AVCodecParameters* par = impl_->fmt->streams[impl_->stream_idx]->codecpar;
  const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
  if (!decoder) {
    close();
    return false;
  }
  impl_->ctx = avcodec_alloc_context3(decoder);
  if (!impl_->ctx || avcodec_parameters_to_context(impl_->ctx, par) < 0 ||
      avcodec_open2(impl_->ctx, decoder, nullptr) < 0) {
    close();
    return false;
  }
  impl_->pkt = av_packet_alloc();
  impl_->frame = av_frame_alloc();
  if (!impl_->pkt || !impl_->frame) {
    close();
    return false;
  }

  impl_->source_rate = impl_->ctx->sample_rate;
  impl_->SourceChannels = impl_->ctx->ch_layout.nb_channels;
  impl_->eof = false;
  impl_->flushed = false;
  return true;
}

void AudioDecoder::close() {
  if (!impl_) return;
  if (impl_->swr) swr_free(&impl_->swr);
  if (impl_->frame) av_frame_free(&impl_->frame);
  if (impl_->pkt) av_packet_free(&impl_->pkt);
  if (impl_->ctx) avcodec_free_context(&impl_->ctx);
  if (impl_->fmt) avformat_close_input(&impl_->fmt);
  *impl_ = Impl();
}

bool AudioDecoder::IsOpen() const { return impl_ && impl_->ctx != nullptr; }

bool AudioDecoder::HasAudio() const { return IsOpen(); }

int AudioDecoder::SourceSampleRate() const {
  return impl_ ? impl_->source_rate : 0;
}

int AudioDecoder::SourceChannels() const {
  return impl_ ? impl_->SourceChannels : 0;
}

void AudioDecoder::SetOutputFormat(int sample_rate, int channels,
                                     PcmFormat format) {
  if (!impl_) return;
  impl_->out_rate = sample_rate > 0 ? sample_rate : 48000;
  impl_->out_channels = channels > 0 ? channels : 2;
  impl_->outFormat = toAv(format);
  if (impl_->swr) swr_free(&impl_->swr);
}

int AudioDecoder::OutputSampleRate() const {
  return impl_ ? impl_->out_rate : 0;
}

int AudioDecoder::OutputChannels() const {
  return impl_ ? impl_->out_channels : 0;
}

void AudioDecoder::SeekTo(double seconds) {
  if (!IsOpen()) return;
  AVStream* stream = impl_->fmt->streams[impl_->stream_idx];
  const int64_t timestamp =
      static_cast<int64_t>(seconds / av_q2d(stream->time_base));
  if (av_seek_frame(impl_->fmt, impl_->stream_idx, timestamp,
                    AVSEEK_FLAG_BACKWARD) < 0) {
    return;
  }
  avcodec_flush_buffers(impl_->ctx);
  if (impl_->swr) swr_convert(impl_->swr, nullptr, 0, nullptr, 0);
  impl_->eof = false;
  impl_->flushed = false;
}

int AudioDecoder::decode(uint8_t* out, int max_bytes) {
  if (!IsOpen() || !out || max_bytes <= 0) return 0;

  const int bytes_per_frame =
      impl_->out_channels * av_get_bytes_per_sample(impl_->outFormat);
  if (bytes_per_frame <= 0) return 0;

  int written = 0;
  while (written + bytes_per_frame <= max_bytes) {
    // Drain samples buffered by the resampler first.
    if (impl_->swr) {
      const int buffered = swr_get_out_samples(impl_->swr, 0);
      if (buffered > 0) {
        const int max_frames = (max_bytes - written) / bytes_per_frame;
        uint8_t* dst = out + written;
        const int converted =
            swr_convert(impl_->swr, &dst, max_frames, nullptr, 0);
        if (converted <= 0) break;
        written += converted * bytes_per_frame;
        continue;
      }
    }

    const int ret = avcodec_receive_frame(impl_->ctx, impl_->frame);
    if (ret == AVERROR(EAGAIN)) {
      if (impl_->eof) {
        if (!impl_->flushed) {
          avcodec_send_packet(impl_->ctx, nullptr);
          impl_->flushed = true;
          continue;
        }
        break;
      }
      if (av_read_frame(impl_->fmt, impl_->pkt) < 0) {
        impl_->eof = true;
        continue;
      }
      if (impl_->pkt->stream_index != impl_->stream_idx) {
        av_packet_unref(impl_->pkt);
        continue;
      }
      avcodec_send_packet(impl_->ctx, impl_->pkt);
      av_packet_unref(impl_->pkt);
      continue;
    }
    if (ret < 0) break;

    if (!impl_->swr) impl_->initResampler();
    const int max_frames = (max_bytes - written) / bytes_per_frame;
    uint8_t* dst = out + written;
    const int converted =
        swr_convert(impl_->swr, &dst, max_frames,
                    const_cast<const uint8_t**>(impl_->frame->extended_data),
                    impl_->frame->nb_samples);
    av_frame_unref(impl_->frame);
    if (converted <= 0) break;
    written += converted * bytes_per_frame;
  }
  return written;
}

bool AudioDecoder::AtEnd() const { return impl_ && impl_->eof; }
