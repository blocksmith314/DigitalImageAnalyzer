#include "h264types.h"

std::string H264SPS::ProfileName() const {
  switch (profile_idc) {
    case 66:
      return "Baseline";
    case 77:
      return "Main";
    case 88:
      return "Extended";
    case 100:
      return "High";
    case 110:
      return "High 10";
    case 122:
      return "High 4:2:2";
    case 244:
      return "High 4:4:4";
    case 44:
      return "CAVLC 4:4:4";
    case 83:
      return "Scalable Baseline";
    case 86:
      return "Scalable High";
    case 118:
      return "Multiview High";
    case 128:
      return "Stereo High";
    case 138:
      return "Multiview Depth High";
    case 139:
      return "Enhanced Multiview Depth High";
    case 134:
      return "MFC High";
    case 135:
      return "MFC Depth High";
    default:
      return "Unknown";
  }
}

static bool has_chroma_info(uint32_t profile) {
  switch (profile) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
      return true;
    default:
      return false;
  }
}

static void skipScalingList(BitReader& br, int size) {
  int last_scale = 8;
  int next_scale = 8;
  for (int j = 0; j < size; j++) {
    if (next_scale != 0) {
      const int64_t delta = br.ReadSe();
      next_scale = (int)((last_scale + delta + 256) % 256);
    }
    last_scale = (next_scale == 0) ? last_scale : next_scale;
  }
}

static void skipHrdParameters(BitReader& br) {
  const uint32_t cpb_cnt_minus1 = (uint32_t)br.ReadUe();
  br.ReadBits(4);  // bit_rate_scale
  br.ReadBits(4);  // cpb_size_scale
  for (uint32_t i = 0; i <= cpb_cnt_minus1; i++) {
    br.ReadUe();   // bit_rate_value_minus1
    br.ReadUe();   // cpb_size_value_minus1
    br.ReadBit();  // cbr_flag
  }
  br.ReadBits(5);  // initial_cpb_removal_delay_length_minus1
  br.ReadBits(5);  // cpb_removal_delay_length_minus1
  br.ReadBits(5);  // dpb_output_delay_length_minus1
  br.ReadBits(5);  // time_offset_length
}

static void parseVui(BitReader& br, H264SPS& out) {
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
      br.ReadBits(8);    // matrix_coefficients
    }
  }
  if (br.ReadBit()) {  // chroma_loc_info_present_flag
    br.ReadUe();       // chroma_sample_loc_type_top_field
    br.ReadUe();       // chroma_sample_loc_type_bottom_field
  }
  out.timing_info_present_flag = br.ReadBit();
  if (out.timing_info_present_flag) {
    out.num_units_in_tick = (uint32_t)br.ReadBits(32);
    out.time_scale = (uint32_t)br.ReadBits(32);
    out.fixed_frame_rate_flag = br.ReadBit();
  }
  out.nal_hrd_parameters_present_flag = br.ReadBit();
  if (out.nal_hrd_parameters_present_flag) skipHrdParameters(br);
  out.vcl_hrd_parameters_present_flag = br.ReadBit();
  if (out.vcl_hrd_parameters_present_flag) skipHrdParameters(br);
  if (out.nal_hrd_parameters_present_flag ||
      out.vcl_hrd_parameters_present_flag)
    br.ReadBit();  // low_delay_hrd_flag
  br.ReadBit();    // pic_struct_present_flag
  out.bitstream_restriction_flag = br.ReadBit();
  if (out.bitstream_restriction_flag) {
    br.ReadBit();  // motion_vectors_over_pic_boundaries_flag
    br.ReadUe();   // max_bytes_per_pic_denom
    br.ReadUe();   // max_bits_per_mb_denom
    br.ReadUe();   // log2_max_mv_length_horizontal
    br.ReadUe();   // log2_max_mv_length_vertical
    out.max_num_reorder_frames = (uint32_t)br.ReadUe();
    out.max_dec_frame_buffering = (uint32_t)br.ReadUe();
  }
}

bool ParseH264Sps(const NALUnit& nal, const uint8_t* data, H264SPS& out) {
  if (nal.size < nal.start_code_len + 2) return false;

  const size_t rbsp_offset = nal.start_code_len + 1;
  BitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.profile_idc = (uint8_t)br.ReadBits(8);
  out.constraint_set0_flag = br.ReadBit();
  out.constraint_set1_flag = br.ReadBit();
  out.constraint_set2_flag = br.ReadBit();
  out.constraint_set3_flag = br.ReadBit();
  out.constraint_set4_flag = br.ReadBit();
  out.constraint_set5_flag = br.ReadBit();
  br.ReadBits(2);  // reserved_zero_2bits
  out.level_idc = (uint8_t)br.ReadBits(8);
  out.sps_id = (uint8_t)br.ReadUe();

  if (has_chroma_info(out.profile_idc)) {
    out.chroma_format_idc = (uint32_t)br.ReadUe();
    if (out.chroma_format_idc == 3)
      out.separate_colour_plane_flag = br.ReadBit();
    out.bit_depth_luma_minus8 = (uint32_t)br.ReadUe();
    out.bit_depth_chroma_minus8 = (uint32_t)br.ReadUe();
    out.qpprime_y_zero_transform_bypass_flag = br.ReadBit();
    out.seq_scaling_matrix_present_flag = br.ReadBit();
    if (out.seq_scaling_matrix_present_flag) {
      const int num_lists = (out.chroma_format_idc != 3) ? 8 : 12;
      for (int i = 0; i < num_lists; i++) {
        if (br.ReadBit()) skipScalingList(br, (i < 6) ? 16 : 64);
      }
    }
  }

  out.log2_max_frame_num_minus4 = (uint32_t)br.ReadUe();
  out.pic_order_cnt_type = (uint32_t)br.ReadUe();

  if (out.pic_order_cnt_type == 0) {
    out.log2_max_pic_order_cnt_lsb_minus4 = (uint32_t)br.ReadUe();
  } else if (out.pic_order_cnt_type == 1) {
    out.delta_pic_order_always_zero_flag = br.ReadBit();
    out.offset_for_non_ref_pic = (int32_t)br.ReadSe();
    out.offset_for_top_to_bottom_field = (int32_t)br.ReadSe();
    out.num_ref_frames_in_pic_order_cnt_cycle = (uint32_t)br.ReadUe();
    for (uint32_t i = 0; i < out.num_ref_frames_in_pic_order_cnt_cycle; i++)
      br.ReadSe();
  }

  out.max_num_ref_frames = (uint32_t)br.ReadUe();
  out.gaps_in_frame_num_value_allowed_flag = br.ReadBit();
  const uint32_t width_in_mbs = (uint32_t)br.ReadUe();
  const uint32_t height_in_map_units = (uint32_t)br.ReadUe();

  out.frame_mbs_only_flag = br.ReadBit();
  if (!out.frame_mbs_only_flag)
    out.mb_adaptive_frame_field_flag = br.ReadBit();
  out.direct_8x8_inference_flag = br.ReadBit();

  out.width = (width_in_mbs + 1) * 16;
  out.height =
      (2 - (out.frame_mbs_only_flag ? 1 : 0)) * (height_in_map_units + 1) * 16;

  out.has_cropping = br.ReadBit();
  if (out.has_cropping) {
    out.crop_left = (uint32_t)br.ReadUe();
    out.crop_right = (uint32_t)br.ReadUe();
    out.crop_top = (uint32_t)br.ReadUe();
    out.crop_bottom = (uint32_t)br.ReadUe();

    uint32_t sub_width_c = 1, subHeightC = 1;
    if (out.chroma_format_idc == 1) {
      sub_width_c = 2;
      subHeightC = 2;
    } else if (out.chroma_format_idc == 2) {
      sub_width_c = 2;
      subHeightC = 1;
    }

    uint32_t crop_unit_x, cropUnitY;
    if (out.chroma_format_idc == 0 || out.separate_colour_plane_flag) {
      crop_unit_x = 1;
      cropUnitY = 2 - (out.frame_mbs_only_flag ? 1 : 0);
    } else {
      crop_unit_x = sub_width_c;
      cropUnitY = subHeightC * (2 - (out.frame_mbs_only_flag ? 1 : 0));
    }
    out.width -= crop_unit_x * (out.crop_left + out.crop_right);
    out.height -= cropUnitY * (out.crop_top + out.crop_bottom);
  }

  out.vui_present_flag = br.ReadBit();
  if (out.vui_present_flag) parseVui(br, out);
  return true;
}

bool ParseH264Pps(const NALUnit& nal, const uint8_t* data, H264PPS& out) {
  if (nal.size < nal.start_code_len + 2) return false;

  const size_t rbsp_offset = nal.start_code_len + 1;
  BitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.pps_id = (uint8_t)br.ReadUe();
  out.sps_id = (uint8_t)br.ReadUe();
  out.entropy_coding_mode_flag = br.ReadBit();
  out.bottom_field_pic_order_in_frame_present_flag = br.ReadBit();
  out.num_slice_groups_minus1 = (uint32_t)br.ReadUe();
  if (out.num_slice_groups_minus1 > 0) {
    out.slice_group_map_type = (uint32_t)br.ReadUe();
    const uint32_t groups = out.num_slice_groups_minus1 + 1;
    if (out.slice_group_map_type == 0) {
      for (uint32_t i = 0; i < groups; i++) br.ReadUe();  // run_length_minus1
    } else if (out.slice_group_map_type == 2) {
      for (uint32_t i = 0; i < groups; i++) {
        br.ReadUe();  // top_left
        br.ReadUe();  // bottom_right
      }
    } else if (out.slice_group_map_type == 3 || out.slice_group_map_type == 4 ||
               out.slice_group_map_type == 5) {
      br.ReadBit();  // slice_group_change_direction_flag
      br.ReadUe();   // slice_group_change_rate_minus1
    } else if (out.slice_group_map_type == 6) {
      const uint32_t pic_size_in_map_units_minus1 = (uint32_t)br.ReadUe();
      int bits = 0;
      while ((1u << bits) < groups) bits++;
      for (uint32_t i = 0; i <= pic_size_in_map_units_minus1; i++)
        br.ReadBits(bits);
    }
  }
  out.num_ref_idx_l0_default_active_minus1 = (uint32_t)br.ReadUe();
  out.num_ref_idx_l1_default_active_minus1 = (uint32_t)br.ReadUe();
  out.weighted_pred_flag = br.ReadBit();
  out.weighted_bipred_idc = (uint8_t)br.ReadBits(2);
  out.pic_init_qp_minus26 = (int32_t)br.ReadSe();
  out.pic_init_qs_minus26 = (int32_t)br.ReadSe();
  out.chroma_qp_index_offset = (int32_t)br.ReadSe();
  out.deblocking_filter_control_present_flag = br.ReadBit();
  out.constrained_intra_pred_flag = br.ReadBit();
  out.redundant_pic_cnt_present_flag = br.ReadBit();

  if (br.MoreRbspData()) {
    out.transform_8x8_mode_flag = br.ReadBit();
    out.pic_scaling_matrix_present_flag = br.ReadBit();
    if (out.pic_scaling_matrix_present_flag) {
      const int num_lists = 6 + (out.transform_8x8_mode_flag ? 2 : 0);  // 4:2:0
      for (int i = 0; i < num_lists; i++) {
        if (br.ReadBit()) skipScalingList(br, (i < 6) ? 16 : 64);
      }
    }
    out.second_chroma_qp_index_offset = (int32_t)br.ReadSe();
  }
  return true;
}

bool ParseH264SliceHeader(const NALUnit& nal, const uint8_t* data,
                             const H264SPS& sps, H264SliceHeader& out) {
  if (nal.size < nal.start_code_len + 5) return false;
  const size_t rbsp_offset = nal.start_code_len + 1;
  RbspBitReader br(data + rbsp_offset, nal.size - rbsp_offset);

  out.first_mb_in_slice = (uint32_t)br.ReadUe();
  out.slice_type = (uint32_t)br.ReadUe();
  (void)br.ReadUe();  // pic_parameter_set_id

  const uint32_t frame_num_bits = sps.log2_max_frame_num_minus4 + 4;
  out.frame_num = (uint32_t)br.ReadBits(frame_num_bits);

  if (!sps.frame_mbs_only_flag) {
    const bool field_pic_flag = br.ReadBit();
    if (field_pic_flag) br.ReadBit();  // bottom_field_flag
  }
  if (nal.nal_type == 5) (void)br.ReadUe();  // idr_pic_id

  if (sps.pic_order_cnt_type == 0) {
    const uint32_t poc_bits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
    out.pic_order_cnt_lsb = (uint32_t)br.ReadBits(poc_bits);
  }
  return true;
}
