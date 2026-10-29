#include "h265types.h"

#include <algorithm>

std::string H265SPS::ProfileName() const {
  switch (profile_idc) {
    case 1:
      return "Main";
    case 2:
      return "Main 10";
    case 3:
      return "Main Still Picture";
    case 4:
      return "Main 4:2:2 10";
    case 5:
      return "Main 4:4:4 10";
    case 6:
      return "Main 12";
    case 7:
      return "Main 4:2:2 12";
    case 8:
      return "Main 4:4:4 12";
    case 9:
      return "Main 4:4:4 16 Intra";
    default:
      return "Unknown";
  }
}

namespace {

void skipSubLayerProfile(BitReader& br) {
  br.ReadBits(2);                             // profile_space
  br.ReadBits(1);                             // tier_flag
  br.ReadBits(5);                             // profile_idc
  for (int i = 0; i < 32; i++) br.ReadBit();  // compatibility flags
  br.ReadBits(4);   // progressive / interlaced / non_packed / frame_only
  br.ReadBits(43);  // constraint flags
  br.ReadBits(1);   // inbld
}

void skipSubLayerPtl(BitReader& br, uint8_t max_sub_layers_minus1) {
  uint8_t sub_profile[8] = {0};
  uint8_t sub_level[8] = {0};
  for (int i = 0; i < max_sub_layers_minus1; i++) {
    sub_profile[i] = br.ReadBit();
    sub_level[i] = br.ReadBit();
  }
  if (max_sub_layers_minus1 > 0) {
    for (int i = max_sub_layers_minus1; i < 8; i++) br.ReadBits(2);
  }
  for (int i = 0; i < max_sub_layers_minus1; i++) {
    if (sub_profile[i]) skipSubLayerProfile(br);
    if (sub_level[i]) br.ReadBits(8);  // sub_layer_level_idc
  }
}

void skipGeneralPtlFields(BitReader& br) {
  br.ReadBits(2);                             // profile_space
  br.ReadBits(1);                             // tier_flag
  br.ReadBits(5);                             // profile_idc
  for (int i = 0; i < 32; i++) br.ReadBit();  // compatibility flags
  br.ReadBits(4);   // progressive / interlaced / non_packed / frame_only
  br.ReadBits(43);  // constraint flags
  br.ReadBits(1);   // inbld
  br.ReadBits(8);   // level_idc
}

void parseGeneralPtl(BitReader& br, H265SPS& out) {
  out.profile_space = (uint8_t)br.ReadBits(2);
  out.tier_flag = br.ReadBit();
  out.profile_idc = (uint8_t)br.ReadBits(5);
  for (int i = 0; i < 32; i++)
    br.ReadBit();  // general_profile_compatibility_flags
  out.progressive_source_flag = br.ReadBit();
  out.interlaced_source_flag = br.ReadBit();
  out.non_packed_constraint_flag = br.ReadBit();
  out.frame_only_constraint_flag = br.ReadBit();
  br.ReadBits(43);  // constraint flags
  br.ReadBits(1);   // general_inbld_flag
  out.level_idc = (uint8_t)br.ReadBits(8);
  skipSubLayerPtl(br, out.max_sub_layers_minus1);
}

void skipScalingListData(BitReader& br) {
  for (int size_id = 0; size_id < 4; size_id++) {
    for (int matrix_id = 0; matrix_id < 6;
         matrix_id += (size_id == 3) ? 3 : 1) {
      const bool pred_mode = br.ReadBit();
      if (!pred_mode) {
        br.ReadUe();  // scaling_list_pred_matrix_id_delta
      } else {
        const int coef_num = std::min(64, 1 << (4 + (size_id << 1)));
        if (size_id > 1) br.ReadSe();  // scaling_list_dc_coef_minus8
        for (int i = 0; i < coef_num; i++) br.ReadSe();
      }
    }
  }
}

void skipHrdParameters(BitReader& br, uint8_t max_sub_layers_minus1) {
  const bool nal_hrd = br.ReadBit();
  const bool vcl_hrd = br.ReadBit();
  bool sub_pic = false;
  if (nal_hrd || vcl_hrd) {
    sub_pic = br.ReadBit();
    if (sub_pic) {
      br.ReadBits(8);  // tick_divisor_minus2
      br.ReadBits(5);  // du_cpb_removal_delay_increment_length_minus1
      br.ReadBit();    // sub_pic_cpb_params_in_pic_timing_sei_flag
      br.ReadBits(5);  // dpb_output_delay_du_length_minus1
    }
    br.ReadBits(4);               // bit_rate_scale
    br.ReadBits(4);               // cpb_size_scale
    if (sub_pic) br.ReadBits(4);  // cpb_size_du_scale
    br.ReadBits(5);               // initial_cpb_removal_delay_length_minus1
    br.ReadBits(5);               // au_cpb_removal_delay_length_minus1
    br.ReadBits(5);               // dpb_output_delay_length_minus1
  }
  for (int i = 0; i <= max_sub_layers_minus1; i++) {
    const bool fixed_general = br.ReadBit();
    bool fixed_within = fixed_general;
    if (!fixed_general) fixed_within = br.ReadBit();
    bool low_delay = false;
    if (fixed_within) {
      br.ReadUe();  // elemental_duration_in_tc_minus1
    } else {
      low_delay = br.ReadBit();
    }
    uint32_t cpb_cnt_minus1 = 0;
    if (!low_delay) cpb_cnt_minus1 = (uint32_t)br.ReadUe();
    for (int pass = 0; pass < 2; pass++) {
      if (pass == 0 && !nal_hrd) continue;
      if (pass == 1 && !vcl_hrd) continue;
      for (uint32_t j = 0; j <= cpb_cnt_minus1; j++) {
        br.ReadUe();  // bit_rate_value_minus1
        br.ReadUe();  // cpb_size_value_minus1
        if (sub_pic) {
          br.ReadUe();  // cpb_size_du_value_minus1
          br.ReadUe();  // bit_rate_du_value_minus1
        }
        br.ReadBit();  // cbr_flag
      }
    }
  }
}

void parseVui(BitReader& br, H265SPS& out) {
  out.aspect_ratio_info_present_flag = br.ReadBit();
  if (out.aspect_ratio_info_present_flag) {
    out.aspect_ratio_idc = (uint8_t)br.ReadBits(8);
    if (out.aspect_ratio_idc == 255) {
      br.ReadBits(16);  // sar_width
      br.ReadBits(16);  // sar_height
    }
  }
  if (br.ReadBit())
    br.ReadBit();  // overscan_info_present_flag, overscan_appropriate_flag
  out.video_signal_type_present_flag = br.ReadBit();
  if (out.video_signal_type_present_flag) {
    out.video_format = (uint8_t)br.ReadBits(3);
    out.video_full_range_flag = br.ReadBit();
    if (br.ReadBit()) {  // colour_description_present_flag
      br.ReadBits(8);    // colour_primaries
      br.ReadBits(8);    // transfer_characteristics
      br.ReadBits(8);    // matrix_coeffs
    }
  }
  if (br.ReadBit()) {  // chroma_loc_info_present_flag
    br.ReadUe();       // chroma_sample_loc_type_top_field
    br.ReadUe();       // chroma_sample_loc_type_bottom_field
  }
  br.ReadBit();        // neutral_chroma_indication_flag
  br.ReadBit();        // field_seq_flag
  br.ReadBit();        // frame_field_info_present_flag
  if (br.ReadBit()) {  // default_display_window_flag
    br.ReadUe();       // def_disp_win_left_offset
    br.ReadUe();       // def_disp_win_right_offset
    br.ReadUe();       // def_disp_win_top_offset
    br.ReadUe();       // def_disp_win_bottom_offset
  }
  out.vui_timing_info_present_flag = br.ReadBit();
  if (out.vui_timing_info_present_flag) {
    out.vui_num_units_in_tick = (uint32_t)br.ReadBits(32);
    out.vui_time_scale = (uint32_t)br.ReadBits(32);
    if (br.ReadBit()) br.ReadUe();  // vui_poc_proportional_to_timing_flag
    if (br.ReadBit()) skipHrdParameters(br, out.max_sub_layers_minus1);
  }
  out.bitstream_restriction_flag = br.ReadBit();
  if (out.bitstream_restriction_flag) {
    br.ReadBit();  // tiles_fixed_structure_flag
    br.ReadBit();  // motion_vectors_over_pic_boundaries_flag
    br.ReadBit();  // restricted_ref_pic_lists_flag
    br.ReadUe();   // min_spatial_segmentation_idc
    br.ReadUe();   // max_bytes_per_pic_denom
    br.ReadUe();   // max_bits_per_min_cu_denom
    br.ReadUe();   // log2_max_mv_length_horizontal
    br.ReadUe();   // log2_max_mv_length_vertical
  }
}

struct ShortTermRps {
  std::vector<int> poc_s0;
  std::vector<int> poc_s1;
  std::vector<bool> used_s0;
  std::vector<bool> used_s1;
  uint32_t num_delta_pocs() const {
    return (uint32_t)(poc_s0.size() + poc_s1.size());
  }
};

void parseShortTermRefPicSet(BitReader& br, uint32_t idx,
                             const std::vector<ShortTermRps>& rps_list,
                             std::vector<ShortTermRps>& out) {
  ShortTermRps rps;
  if (idx != 0) {
    const bool inter_pred = br.ReadBit();
    if (inter_pred) {
      const uint32_t ref_idx = idx - 1;
      if (ref_idx >= rps_list.size())
        return;  // malformed; stop parsing this set
      const ShortTermRps& ref = rps_list[ref_idx];
      const bool delta_rps_sign = br.ReadBit();
      const uint32_t abs_delta_rps_minus1 = (uint32_t)br.ReadUe();
      const int delta_rps =
          (1 - 2 * (int)delta_rps_sign) * (int)(abs_delta_rps_minus1 + 1);

      const uint32_t num_delta = ref.num_delta_pocs();
      std::vector<bool> used(num_delta + 1, false);
      std::vector<bool> use_delta(num_delta + 1, true);
      for (uint32_t j = 0; j <= num_delta; j++) {
        used[j] = br.ReadBit();
        if (!used[j]) use_delta[j] = br.ReadBit();
      }

      const int num_neg_ref = (int)ref.poc_s0.size();
      const int num_pos_ref = (int)ref.poc_s1.size();

      for (int j = num_pos_ref - 1; j >= 0; j--) {
        const int d_poc = ref.poc_s1[j] + delta_rps;
        const int flag_idx = num_neg_ref + j;
        if (d_poc < 0 && use_delta[flag_idx]) {
          rps.poc_s0.push_back(d_poc);
          rps.used_s0.push_back(used[flag_idx]);
        } else if (d_poc > 0) {
          rps.poc_s1.push_back(d_poc);
          rps.used_s1.push_back(used[flag_idx]);
        }
      }
      for (int j = 0; j < num_neg_ref; j++) {
        const int d_poc = ref.poc_s0[j] + delta_rps;
        if (d_poc < 0 && use_delta[j]) {
          rps.poc_s0.push_back(d_poc);
          rps.used_s0.push_back(used[j]);
        } else if (d_poc > 0) {
          rps.poc_s1.push_back(d_poc);
          rps.used_s1.push_back(used[j]);
        }
      }
      if (delta_rps < 0 && use_delta[num_delta]) {
        rps.poc_s0.push_back(delta_rps);
        rps.used_s0.push_back(used[num_delta]);
      } else if (delta_rps > 0) {
        rps.poc_s1.push_back(delta_rps);
        rps.used_s1.push_back(used[num_delta]);
      }

      out.push_back(std::move(rps));
      return;
    }
  }

  const uint32_t num_neg = (uint32_t)br.ReadUe();
  const uint32_t num_pos = (uint32_t)br.ReadUe();
  int prev = 0;
  for (uint32_t i = 0; i < num_neg; i++) {
    prev -= (int)br.ReadUe() + 1;
    rps.poc_s0.push_back(prev);
    rps.used_s0.push_back(br.ReadBit());
  }
  prev = 0;
  for (uint32_t i = 0; i < num_pos; i++) {
    prev += (int)br.ReadUe() + 1;
    rps.poc_s1.push_back(prev);
    rps.used_s1.push_back(br.ReadBit());
  }
  out.push_back(std::move(rps));
}

}  // namespace

bool ParseH265Vps(const NALUnit& nal, const uint8_t* data, H265VPS& out) {
  if (nal.size < nal.start_code_len + 3) return false;

  const size_t rbsp_offset = nal.start_code_len + 2;
  BitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.vps_id = (uint8_t)br.ReadBits(4);
  out.base_layer_internal_flag = br.ReadBit();
  out.base_layer_available_flag = br.ReadBit();
  out.max_layers_minus1 = (uint8_t)br.ReadBits(6);
  out.max_sub_layers_minus1 = (uint8_t)br.ReadBits(3);
  out.temporal_id_nesting_flag = br.ReadBit();
  br.ReadBits(16);  // vps_reserved_0xffff_16bits

  skipGeneralPtlFields(br);
  skipSubLayerPtl(br, out.max_sub_layers_minus1);

  const bool sub_layer_ordering_info_present = br.ReadBit();
  for (int i =
           (sub_layer_ordering_info_present ? 0 : out.max_sub_layers_minus1);
       i <= out.max_sub_layers_minus1; i++) {
    br.ReadUe();  // vps_max_dec_pic_buffering_minus1
    br.ReadUe();  // vps_max_num_reorder_pics
    br.ReadUe();  // vps_max_latency_increase_plus1
  }
  out.max_layer_id = (uint8_t)br.ReadBits(6);
  return true;
}

bool ParseH265Sps(const NALUnit& nal, const uint8_t* data, H265SPS& out) {
  if (nal.size < nal.start_code_len + 3) return false;

  const size_t rbsp_offset = nal.start_code_len + 2;
  BitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.vps_id = (uint8_t)br.ReadBits(4);
  out.max_sub_layers_minus1 = (uint8_t)br.ReadBits(3);
  out.temporal_id_nesting_flag = br.ReadBit();
  parseGeneralPtl(br, out);

  out.sps_id = (uint8_t)br.ReadUe();
  out.chroma_format_idc = (uint8_t)br.ReadUe();
  if (out.chroma_format_idc == 3)
    out.separate_colour_plane_flag = br.ReadBit();

  out.pic_width_in_luma_samples = (uint32_t)br.ReadUe();
  out.pic_height_in_luma_samples = (uint32_t)br.ReadUe();
  out.has_cropping = br.ReadBit();  // conformance_window_flag
  if (out.has_cropping) {
    out.crop_left = (uint32_t)br.ReadUe();
    out.crop_right = (uint32_t)br.ReadUe();
    out.crop_top = (uint32_t)br.ReadUe();
    out.crop_bottom = (uint32_t)br.ReadUe();
  }

  out.width = out.pic_width_in_luma_samples;
  out.height = out.pic_height_in_luma_samples;
  if (out.has_cropping) {
    uint32_t sub_width_c = 1, subHeightC = 1;
    if (out.chroma_format_idc == 1) {
      sub_width_c = 2;
      subHeightC = 2;
    } else if (out.chroma_format_idc == 2) {
      sub_width_c = 2;
      subHeightC = 1;
    }
    out.width -= sub_width_c * (out.crop_left + out.crop_right);
    out.height -= subHeightC * (out.crop_top + out.crop_bottom);
  }

  out.bit_depth_luma_minus8 = (uint32_t)br.ReadUe();
  out.bit_depth_chroma_minus8 = (uint32_t)br.ReadUe();
  out.log2_max_pic_order_cnt_lsb_minus4 = (uint32_t)br.ReadUe();

  out.sub_layer_ordering_info_present_flag = br.ReadBit();
  for (int i = (out.sub_layer_ordering_info_present_flag
                    ? 0
                    : out.max_sub_layers_minus1);
       i <= out.max_sub_layers_minus1; i++) {
    out.max_dec_pic_buffering_minus1 = (uint32_t)br.ReadUe();
    out.max_num_reorder_pics = (uint32_t)br.ReadUe();
    out.max_latency_increase_plus1 = (uint32_t)br.ReadUe();
  }

  out.log2_min_luma_coding_block_size_minus3 = (uint32_t)br.ReadUe();
  out.log2_diff_max_min_luma_coding_block_size = (uint32_t)br.ReadUe();
  out.log2_min_transform_block_size_minus2 = (uint32_t)br.ReadUe();
  out.log2_diff_max_min_transform_block_size = (uint32_t)br.ReadUe();
  out.max_transform_hierarchy_depth_inter = (uint32_t)br.ReadUe();
  out.max_transform_hierarchy_depth_intra = (uint32_t)br.ReadUe();

  out.scaling_list_enabled_flag = br.ReadBit();
  if (out.scaling_list_enabled_flag) {
    const bool scaling_list_data_present = br.ReadBit();
    if (scaling_list_data_present) skipScalingListData(br);
  }
  out.amp_enabled_flag = br.ReadBit();
  out.sample_adaptive_offset_enabled_flag = br.ReadBit();
  out.pcm_enabled_flag = br.ReadBit();
  if (out.pcm_enabled_flag) {
    br.ReadBits(4);  // pcm_sample_bit_depth_luma_minus1
    br.ReadBits(4);  // pcm_sample_bit_depth_chroma_minus1
    br.ReadUe();     // log2_min_pcm_luma_coding_block_size_minus3
    br.ReadUe();     // log2_diff_max_min_pcm_luma_coding_block_size
    br.ReadBit();    // pcm_loop_filter_disabled_flag
  }

  out.num_short_term_ref_pic_sets = (uint32_t)br.ReadUe();
  std::vector<ShortTermRps> rps_list;
  for (uint32_t i = 0; i < out.num_short_term_ref_pic_sets; i++)
    parseShortTermRefPicSet(br, i, rps_list, rps_list);

  out.long_term_ref_pics_present_flag = br.ReadBit();
  if (out.long_term_ref_pics_present_flag) {
    out.num_long_term_ref_pics_sps = (uint32_t)br.ReadUe();
    const int poc_bits = (int)out.log2_max_pic_order_cnt_lsb_minus4 + 4;
    for (uint32_t i = 0; i < out.num_long_term_ref_pics_sps; i++) {
      br.ReadBits(poc_bits);  // lt_ref_pic_poc_lsb_sps
      br.ReadBit();           // used_by_curr_pic_lt_sps_flag
    }
  }
  out.sps_temporal_mvp_enabled_flag = br.ReadBit();
  out.strong_intra_smoothing_enabled_flag = br.ReadBit();
  out.vui_present_flag = br.ReadBit();
  if (out.vui_present_flag) parseVui(br, out);
  out.sps_extension_present_flag = br.ReadBit();
  return true;
}

bool ParseH265SliceSegmentHeader(const NALUnit& nal, const uint8_t* data,
                                     const H265SPS& sps, const H265PPS& pps,
                                     H265SliceSegmentHeader& out) {
  if (nal.size < nal.start_code_len + 3) return false;

  const size_t rbsp_offset = nal.start_code_len + 2;
  RbspBitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.first_slice_segment_in_pic_flag = br.ReadBit();
  out.is_irap = (nal.nal_type >= 16 && nal.nal_type <= 23);
  out.is_idr = (nal.nal_type == 19 || nal.nal_type == 20);
  if (out.is_irap) br.ReadBit();  // no_output_of_prior_pics_flag
  out.slice_pic_parameter_set_id = (uint32_t)br.ReadUe();

  if (!out.first_slice_segment_in_pic_flag) {
    if (pps.dependent_slice_segments_enabled_flag)
      out.dependent = br.ReadBit();
    const uint32_t log2_ctu = sps.log2_min_luma_coding_block_size_minus3 + 3 +
                              sps.log2_diff_max_min_luma_coding_block_size;
    const uint32_t ctu_size = 1u << log2_ctu;
    const uint32_t ctbs_x =
        (sps.pic_width_in_luma_samples + ctu_size - 1) / ctu_size;
    const uint32_t ctbs_y =
        (sps.pic_height_in_luma_samples + ctu_size - 1) / ctu_size;
    const uint32_t pic_size_ctbs = std::max(1u, ctbs_x * ctbs_y);
    int addr_bits = 0;
    while ((1u << addr_bits) < pic_size_ctbs) addr_bits++;
    br.ReadBits(addr_bits);  // slice_segment_address
  }

  if (!out.dependent) {
    for (int i = 0; i < (int)pps.num_extra_slice_header_bits; i++)
      br.ReadBit();
    out.slice_type = (uint32_t)br.ReadUe();
    if (pps.output_flag_present_flag) br.ReadBit();
    if (sps.separate_colour_plane_flag) br.ReadBits(2);
    if (!out.is_idr) {
      const uint32_t poc_bits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
      out.pic_order_cnt_lsb = (uint32_t)br.ReadBits(poc_bits);
    }
    out.poc_valid = true;  // IDR pictures have POC 0
  }
  return true;
}

bool ParseH265Pps(const NALUnit& nal, const uint8_t* data, H265PPS& out) {
  if (nal.size < nal.start_code_len + 3) return false;

  const size_t rbsp_offset = nal.start_code_len + 2;
  BitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.pps_id = (uint8_t)br.ReadUe();
  out.sps_id = (uint8_t)br.ReadUe();
  out.dependent_slice_segments_enabled_flag = br.ReadBit();
  out.output_flag_present_flag = br.ReadBit();
  out.num_extra_slice_header_bits = (uint8_t)br.ReadBits(3);
  out.sign_data_hiding_enabled_flag = br.ReadBit();
  out.cabac_init_present_flag = br.ReadBit();
  out.num_ref_idx_l0_default_active_minus1 = (uint32_t)br.ReadUe();
  out.num_ref_idx_l1_default_active_minus1 = (uint32_t)br.ReadUe();
  out.init_qp_minus26 = (int32_t)br.ReadSe();
  out.constrained_intra_pred_flag = br.ReadBit();
  out.transform_skip_enabled_flag = br.ReadBit();
  out.cu_qp_delta_enabled_flag = br.ReadBit();
  if (out.cu_qp_delta_enabled_flag)
    out.diff_cu_qp_delta_depth = (uint32_t)br.ReadUe();
  out.pps_cb_qp_offset = (int32_t)br.ReadSe();
  out.pps_cr_qp_offset = (int32_t)br.ReadSe();
  out.pps_slice_chroma_qp_offsets_present_flag = br.ReadBit();
  out.weighted_pred_flag = br.ReadBit();
  out.weighted_bipred_flag = br.ReadBit();
  out.transquant_bypass_enabled_flag = br.ReadBit();
  out.tiles_enabled_flag = br.ReadBit();
  out.entropy_coding_sync_enabled_flag = br.ReadBit();
  if (out.tiles_enabled_flag) {
    out.num_tile_columns_minus1 = (uint32_t)br.ReadUe();
    out.num_tile_rows_minus1 = (uint32_t)br.ReadUe();
    out.uniform_spacing_flag = br.ReadBit();
    if (!out.uniform_spacing_flag) {
      for (uint32_t i = 0; i < out.num_tile_columns_minus1; i++) br.ReadUe();
      for (uint32_t i = 0; i < out.num_tile_rows_minus1; i++) br.ReadUe();
    }
    br.ReadBit();  // loop_filter_across_tiles_enabled_flag
  }
  out.pps_loop_filter_across_slices_enabled_flag = br.ReadBit();
  out.deblocking_filter_control_present_flag = br.ReadBit();
  if (out.deblocking_filter_control_present_flag) {
    out.deblocking_filter_override_enabled_flag = br.ReadBit();
    out.pps_deblocking_filter_disabled_flag = br.ReadBit();
    if (!out.pps_deblocking_filter_disabled_flag) {
      out.pps_beta_offset_div2 = (int32_t)br.ReadSe();
      out.pps_tc_offset_div2 = (int32_t)br.ReadSe();
    }
  }
  out.pps_scaling_list_data_present_flag = br.ReadBit();
  if (out.pps_scaling_list_data_present_flag) skipScalingListData(br);
  out.lists_modification_present_flag = br.ReadBit();
  out.log2_parallel_merge_level_minus2 = (uint32_t)br.ReadUe();
  out.slice_segment_header_extension_present_flag = br.ReadBit();
  out.pps_extension_present_flag = br.ReadBit();
  return true;
}
