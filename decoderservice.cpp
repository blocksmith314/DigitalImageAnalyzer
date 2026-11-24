#include "decoderservice.h"

#include <cstdlib>
#include <vector>

DecoderService::DecoderService(QObject* parent) : QObject(parent) {}

DecoderService::~DecoderService() = default;

QVariantMap DecoderService::Open(const QString& path, int codec,
                                 QVariantList raw_keyframes) {
  QVariantMap result;
  decoder_ = std::make_unique<FFmpegDecoder>();
  if (!decoder_->OpenFile(path.toStdString(), (CodecType)codec)) {
    result["ok"] = false;
    result["error"] = QString::fromStdString(decoder_->LastError());
    decoder_.reset();
    return result;
  }

  if (raw_keyframes.size() >= 2) {
    std::vector<FFmpegDecoder::RawKeyframe> keyframes;
    for (int i = 0; i + 1 < raw_keyframes.size(); i += 2) {
      FFmpegDecoder::RawKeyframe keyframe;
      keyframe.frame_index = raw_keyframes[i].toInt();
      keyframe.byte_offset = raw_keyframes[i + 1].toLongLong();
      keyframes.push_back(keyframe);
    }
    decoder_->SetRawKeyframeIndex(std::move(keyframes));
  }

  result["ok"] = true;
  result["has_timestamps"] = decoder_->HasTimestamps();
  result["frame_rate"] = decoder_->FrameRate();
  result["duration"] = decoder_->DurationSec();
  return result;
}

QVariantMap DecoderService::Close() {
  if (decoder_) {
    decoder_->close();
    decoder_.reset();
  }
  QVariantMap result;
  result["ok"] = true;
  return result;
}

QVariantMap DecoderService::Seek(int frame) {
  QVariantMap result;
  if (!decoder_) {
    result["ok"] = false;
    return result;
  }
  result["ok"] = decoder_->SeekToFrame(frame);
  const FFmpegDecoder::SeekStats stats = decoder_->LastSeekStats();
  result["used_container"] = stats.used_container;
  result["used_raw_keyframe"] = stats.used_raw_keyframe;
  result["frames_decoded"] = stats.frames_decoded;
  result["seek_io_ms"] = (qint64)stats.seek_io_ms;
  result["decode_ms"] = (qint64)stats.decode_ms;
  return result;
}

QVariantMap DecoderService::DecodeNext() {
  QVariantMap result;
  if (!decoder_) {
    result["ok"] = false;
    return result;
  }
  DecodedFrame frame;
  if (!decoder_->DecodeNextFrame(frame)) {
    result["ok"] = false;
    return result;
  }
  result["ok"] = true;
  result["image"] = frame.image;
  result["pts"] = frame.pts_sec;
  cache_.Insert(decoder_->CurrentFrameIndex() - 1, frame.image, frame.pts_sec);
  return result;
}

void DecoderService::RequestSeek(int frame, quint64 generation) {
  QVariantMap result;
  result["generation"] = generation;
  result["target"] = frame;
  if (!decoder_) {
    result["ok"] = false;
    emit SeekFailed(result);
    return;
  }

  if (const CachedFrame* hit = cache_.Find(frame)) {
    result["ok"] = true;
    result["from_cache"] = true;
    result["image"] = hit->image;
    result["pts"] = hit->pts_sec;
    emit FrameReady(result);
    return;
  }

  if (const CachedFrame* nearest = cache_.Nearest(frame)) {
    QVariantMap preview;
    preview["generation"] = generation;
    preview["target"] = frame;
    preview["image"] = nearest->image;
    preview["from_frame"] = nearest->index;
    emit PreviewReady(preview);
  }

  if (!decoder_->SeekToFrame(frame)) {
    result["ok"] = false;
    emit SeekFailed(result);
    return;
  }
  DecodedFrame decoded;
  if (!decoder_->DecodeNextFrame(decoded)) {
    result["ok"] = false;
    emit SeekFailed(result);
    return;
  }
  const FFmpegDecoder::SeekStats stats = decoder_->LastSeekStats();
  cache_.Insert(frame, decoded.image, decoded.pts_sec);
  result["ok"] = true;
  result["from_cache"] = false;
  result["image"] = decoded.image;
  result["pts"] = decoded.pts_sec;
  result["frames_decoded"] = stats.frames_decoded;
  result["seek_io_ms"] = (qint64)stats.seek_io_ms;
  result["decode_ms"] = (qint64)stats.decode_ms;
  emit FrameReady(result);
}
