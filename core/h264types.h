#ifndef DIA_CORE_H264TYPES_H_
#define DIA_CORE_H264TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "expgolomb.h"
#include "nalunit.h"

struct H264SPS {
  uint8_t profile_idc = 0;
  uint8_t level_idc = 0;
  uint8_t sps_id = 0;
  bool constraint_set0_flag = false;
  bool constraint_set1_flag = false;
  bool constraint_set2_flag = false;
  bool constraint_set3_flag = false;
  bool constraint_set4_flag = false;
  bool constraint_set5_flag = false;
  uint32_t chroma_format_idc = 1;
  bool separate_colour_plane_flag = false;
  uint32_t bit_depth_luma_minus8 = 0;
  uint32_t bit_depth_chroma_minus8 = 0;
  bool qpprime_y_zero_transform_bypass_flag = false;
  bool seq_scaling_matrix_present_flag = false;
  uint32_t log2_max_frame_num_minus4 = 0;
  uint32_t pic_order_cnt_type = 0;
  uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
  bool delta_pic_order_always_zero_flag = false;
  int32_t offset_for_non_ref_pic = 0;
  int32_t offset_for_top_to_bottom_field = 0;
  uint32_t num_ref_frames_in_pic_order_cnt_cycle = 0;
  uint32_t max_num_ref_frames = 0;
  bool gaps_in_frame_num_value_allowed_flag = false;
  uint32_t width = 0;
  uint32_t height = 0;
  bool frame_mbs_only_flag = true;
  bool mb_adaptive_frame_field_flag = false;
  bool direct_8x8_inference_flag = false;
  bool has_cropping = false;
  uint32_t crop_left = 0;
  uint32_t crop_right = 0;
  uint32_t crop_top = 0;
  uint32_t crop_bottom = 0;
  // VUI (subset)
  bool vui_present_flag = false;
  bool aspect_ratio_info_present_flag = false;
  uint8_t aspect_ratio_idc = 0;
  bool video_signal_type_present_flag = false;
  uint8_t video_format = 0;
  bool video_full_range_flag = false;
  bool timing_info_present_flag = false;
  uint32_t num_units_in_tick = 0;
  uint32_t time_scale = 0;
  bool fixed_frame_rate_flag = false;
  bool nal_hrd_parameters_present_flag = false;
  bool vcl_hrd_parameters_present_flag = false;
  bool bitstream_restriction_flag = false;
  uint32_t max_num_reorder_frames = 0;
  uint32_t max_dec_frame_buffering = 0;
  std::string ProfileName() const;
};

struct H264PPS {
  uint8_t pps_id = 0;
  uint8_t sps_id = 0;
  bool entropy_coding_mode_flag = false;
  bool bottom_field_pic_order_in_frame_present_flag = false;
  uint32_t num_slice_groups_minus1 = 0;
  uint32_t slice_group_map_type = 0;
  uint32_t num_ref_idx_l0_default_active_minus1 = 0;
  uint32_t num_ref_idx_l1_default_active_minus1 = 0;
  bool weighted_pred_flag = false;
  uint8_t weighted_bipred_idc = 0;
  int32_t pic_init_qp_minus26 = 0;
  int32_t pic_init_qs_minus26 = 0;
  int32_t chroma_qp_index_offset = 0;
  bool deblocking_filter_control_present_flag = false;
  bool constrained_intra_pred_flag = false;
  bool redundant_pic_cnt_present_flag = false;
  bool transform_8x8_mode_flag = false;
  bool pic_scaling_matrix_present_flag = false;
  int32_t second_chroma_qp_index_offset = 0;
};

bool ParseH264Sps(const NALUnit& nal, const uint8_t* data, H264SPS& out);
bool ParseH264Pps(const NALUnit& nal, const uint8_t* data, H264PPS& out);

struct H264SliceHeader {
  uint32_t first_mb_in_slice = 0;
  uint32_t slice_type = 0;
  uint32_t frame_num = 0;
  uint32_t pic_order_cnt_lsb = 0;
  bool IsIFrame() const { return slice_type % 5 == 2; }
  bool IsBFrame() const { return slice_type % 5 == 1; }
  bool IsPFrame() const {
    return slice_type % 5 == 0 || slice_type % 5 == 3 || slice_type % 5 == 4;
  }
  char FrameTypeChar() const {
    if (IsIFrame()) return 'I';
    if (IsBFrame()) return 'B';
    return 'P';
  }
};

bool ParseH264SliceHeader(const NALUnit& nal, const uint8_t* data,
                             const H264SPS& sps, H264SliceHeader& out);

#endif  // DIA_CORE_H264TYPES_H_
