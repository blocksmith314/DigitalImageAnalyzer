#ifndef DIA_CORE_H265TYPES_H_
#define DIA_CORE_H265TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

#include "expgolomb.h"
#include "nalunit.h"

struct H265VPS {
  uint8_t vps_id = 0;
  bool base_layer_internal_flag = false;
  bool base_layer_available_flag = false;
  uint8_t max_layers_minus1 = 0;
  uint8_t max_sub_layers_minus1 = 0;
  bool temporal_id_nesting_flag = false;
  uint8_t max_layer_id = 0;
};

struct H265SPS {
  uint8_t vps_id = 0;
  uint8_t sps_id = 0;
  uint8_t max_sub_layers_minus1 = 0;
  bool temporal_id_nesting_flag = false;
  uint8_t profile_space = 0;
  bool tier_flag = false;
  uint8_t profile_idc = 0;
  uint8_t level_idc = 0;
  bool progressive_source_flag = false;
  bool interlaced_source_flag = false;
  bool non_packed_constraint_flag = false;
  bool frame_only_constraint_flag = false;
  uint8_t chroma_format_idc = 1;
  bool separate_colour_plane_flag = false;
  uint32_t pic_width_in_luma_samples = 0;
  uint32_t pic_height_in_luma_samples = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  bool has_cropping = false;
  uint32_t crop_left = 0;
  uint32_t crop_right = 0;
  uint32_t crop_top = 0;
  uint32_t crop_bottom = 0;
  uint32_t bit_depth_luma_minus8 = 0;
  uint32_t bit_depth_chroma_minus8 = 0;
  uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
  bool sub_layer_ordering_info_present_flag = false;
  uint32_t max_dec_pic_buffering_minus1 = 0;
  uint32_t max_num_reorder_pics = 0;
  uint32_t max_latency_increase_plus1 = 0;
  uint32_t log2_min_luma_coding_block_size_minus3 = 0;
  uint32_t log2_diff_max_min_luma_coding_block_size = 0;
  uint32_t log2_min_transform_block_size_minus2 = 0;
  uint32_t log2_diff_max_min_transform_block_size = 0;
  uint32_t max_transform_hierarchy_depth_inter = 0;
  uint32_t max_transform_hierarchy_depth_intra = 0;
  bool scaling_list_enabled_flag = false;
  bool amp_enabled_flag = false;
  bool sample_adaptive_offset_enabled_flag = false;
  bool pcm_enabled_flag = false;
  uint32_t num_short_term_ref_pic_sets = 0;
  bool long_term_ref_pics_present_flag = false;
  uint32_t num_long_term_ref_pics_sps = 0;
  bool sps_temporal_mvp_enabled_flag = false;
  bool strong_intra_smoothing_enabled_flag = false;
  // VUI (subset)
  bool vui_present_flag = false;
  bool aspect_ratio_info_present_flag = false;
  uint8_t aspect_ratio_idc = 0;
  bool video_signal_type_present_flag = false;
  uint8_t video_format = 0;
  bool video_full_range_flag = false;
  bool vui_timing_info_present_flag = false;
  uint32_t vui_num_units_in_tick = 0;
  uint32_t vui_time_scale = 0;
  bool bitstream_restriction_flag = false;
  bool sps_extension_present_flag = false;
  std::string ProfileName() const;
};

struct H265PPS {
  uint8_t pps_id = 0;
  uint8_t sps_id = 0;
  bool dependent_slice_segments_enabled_flag = false;
  bool output_flag_present_flag = false;
  uint8_t num_extra_slice_header_bits = 0;
  bool sign_data_hiding_enabled_flag = false;
  bool cabac_init_present_flag = false;
  uint32_t num_ref_idx_l0_default_active_minus1 = 0;
  uint32_t num_ref_idx_l1_default_active_minus1 = 0;
  int32_t init_qp_minus26 = 0;
  bool constrained_intra_pred_flag = false;
  bool transform_skip_enabled_flag = false;
  bool cu_qp_delta_enabled_flag = false;
  uint32_t diff_cu_qp_delta_depth = 0;
  int32_t pps_cb_qp_offset = 0;
  int32_t pps_cr_qp_offset = 0;
  bool pps_slice_chroma_qp_offsets_present_flag = false;
  bool weighted_pred_flag = false;
  bool weighted_bipred_flag = false;
  bool transquant_bypass_enabled_flag = false;
  bool tiles_enabled_flag = false;
  bool entropy_coding_sync_enabled_flag = false;
  uint32_t num_tile_columns_minus1 = 0;
  uint32_t num_tile_rows_minus1 = 0;
  bool uniform_spacing_flag = false;
  bool pps_loop_filter_across_slices_enabled_flag = false;
  bool deblocking_filter_control_present_flag = false;
  bool deblocking_filter_override_enabled_flag = false;
  bool pps_deblocking_filter_disabled_flag = false;
  int32_t pps_beta_offset_div2 = 0;
  int32_t pps_tc_offset_div2 = 0;
  bool pps_scaling_list_data_present_flag = false;
  bool lists_modification_present_flag = false;
  uint32_t log2_parallel_merge_level_minus2 = 0;
  bool slice_segment_header_extension_present_flag = false;
  bool pps_extension_present_flag = false;
};

struct H265SliceSegmentHeader {
  bool first_slice_segment_in_pic_flag = true;
  bool is_irap = false;
  bool is_idr = false;
  bool dependent = false;
  uint32_t slice_pic_parameter_set_id = 0;
  uint32_t slice_type = 1;  // 0 = B, 1 = P, 2 = I
  uint32_t pic_order_cnt_lsb = 0;
  bool poc_valid = false;

  char FrameTypeChar() const {
    if (is_irap || slice_type == 2) return 'I';
    if (slice_type == 0) return 'B';
    return 'P';
  }
};

bool ParseH265Vps(const NALUnit& nal, const uint8_t* data, H265VPS& out);
bool ParseH265Sps(const NALUnit& nal, const uint8_t* data, H265SPS& out);
bool ParseH265Pps(const NALUnit& nal, const uint8_t* data, H265PPS& out);
bool ParseH265SliceSegmentHeader(const NALUnit& nal, const uint8_t* data,
                                     const H265SPS& sps, const H265PPS& pps,
                                     H265SliceSegmentHeader& out);

#endif  // DIA_CORE_H265TYPES_H_
