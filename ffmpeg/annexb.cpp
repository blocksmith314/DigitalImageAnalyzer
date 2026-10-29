#include "annexb.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
}

bool ProbeVideoFile(const std::string& path, CodecType& codec,
                      bool& isRawAnnexB, std::string& error) {
  isRawAnnexB = false;

  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
    error = "Failed to open input";
    return false;
  }
  if (avformat_find_stream_info(fmt, nullptr) < 0) {
    avformat_close_input(&fmt);
    error = "Failed to find stream info";
    return false;
  }

  int video_idx = -1;
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_idx = (int)i;
      break;
    }
  }
  if (video_idx < 0) {
    avformat_close_input(&fmt);
    error = "No video stream found";
    return false;
  }

  const AVCodecParameters* par = fmt->streams[video_idx]->codecpar;
  if (par->codec_id == AV_CODEC_ID_H264) {
    codec = CodecType::kH264;
  } else if (par->codec_id == AV_CODEC_ID_HEVC) {
    codec = CodecType::kH265;
  } else {
    avformat_close_input(&fmt);
    error = "Unsupported video codec (only H.264/H.265)";
    return false;
  }

  const char* name =
      (fmt->iformat && fmt->iformat->name) ? fmt->iformat->name : "";
  isRawAnnexB =
      (std::strcmp(name, "h264") == 0 || std::strcmp(name, "hevc") == 0);

  avformat_close_input(&fmt);
  return true;
}
