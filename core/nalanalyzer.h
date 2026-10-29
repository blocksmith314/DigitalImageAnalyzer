#ifndef DIA_CORE_NALANALYZER_H_
#define DIA_CORE_NALANALYZER_H_

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "h264types.h"
#include "h265types.h"
#include "nalunit.h"

struct NALAnalysis {
  bool is_sps = false;
  bool is_pps = false;
  bool is_vps = false;
  H264SPS h264sps;
  H264PPS h264pps;
  H265SPS h265sps;
  H265PPS h265pps;
  H265VPS h265vps;
  std::string details;
};

struct StreamStats {
  CodecType codec = CodecType::kH264;
  uint64_t total_bytes = 0;
  uint32_t total_nals = 0;
  uint32_t idr_count = 0;
  uint32_t sps_count = 0;
  uint32_t pps_count = 0;
  uint32_t vps_count = 0;
  uint32_t sei_count = 0;
  uint32_t aud_count = 0;
  uint32_t FrameCount = 0;
  uint32_t slice_count = 0;  // number of slice NALs
  uint32_t i_frame_count = 0;
  uint32_t p_frame_count = 0;
  uint32_t b_frame_count = 0;
  uint32_t max_nal_size = 0;
  uint32_t min_nal_size = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string profile;
  std::string level;
};

class BitstreamParser;

class NALAnalyzer {
 public:
  bool AnalyzeAll(BitstreamParser& parser,
                   const std::function<bool(double)>& progress = {});
  NALAnalysis AnalyzeOne(const NALUnit& nal, const uint8_t* data) const;
  const StreamStats& stats() const { return stats_; }
  const std::map<int, H264SPS>& H264SpsMap() const { return h264_sps_map_; }
  const std::map<int, H264PPS>& H264PpsMap() const { return h264_pps_map_; }
  const std::map<int, H265SPS>& H265SpsMap() const { return h265_sps_map_; }
  const std::map<int, H265PPS>& H265PpsMap() const { return h265_pps_map_; }
  const std::map<int, H265VPS>& H265VpsMap() const { return h265_vps_map_; }

 private:
  StreamStats stats_;
  std::map<int, H264SPS> h264_sps_map_;
  std::map<int, H264PPS> h264_pps_map_;
  std::map<int, H265SPS> h265_sps_map_;
  std::map<int, H265PPS> h265_pps_map_;
  std::map<int, H265VPS> h265_vps_map_;
};

#endif  // DIA_CORE_NALANALYZER_H_
