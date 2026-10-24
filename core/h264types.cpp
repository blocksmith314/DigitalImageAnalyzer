#include "h264types.h"

std::string H264SPS::profileName() const {
    switch (profile_idc) {
        case 66: return "Baseline";
        case 77: return "Main";
        case 88: return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4";
        case 44: return "CAVLC 4:4:4";
        case 83: return "Scalable Baseline";
        case 86: return "Scalable High";
        case 118: return "Multiview High";
        case 128: return "Stereo High";
        case 138: return "Multiview Depth High";
        case 139: return "Enhanced Multiview Depth High";
        case 134: return "MFC High";
        case 135: return "MFC Depth High";
        default: return "Unknown";
    }
}

static bool hasChromaInfo(uint32_t profile) {
    switch (profile) {
        case 100: case 110: case 122: case 244:
        case 44: case 83: case 86: case 118: case 128:
        case 138: case 139: case 134: case 135:
            return true;
        default:
            return false;
    }
}

static void skipScalingList(BitReader &br, int size) {
    int lastScale = 8;
    int nextScale = 8;
    for (int j = 0; j < size; j++) {
        if (nextScale != 0) {
            const int64_t delta = br.readSe();
            nextScale = (int)((lastScale + delta + 256) % 256);
        }
        lastScale = (nextScale == 0) ? lastScale : nextScale;
    }
}

static void skipHrdParameters(BitReader &br) {
    const uint32_t cpbCntMinus1 = (uint32_t)br.readUe();
    br.readBits(4); // bit_rate_scale
    br.readBits(4); // cpb_size_scale
    for (uint32_t i = 0; i <= cpbCntMinus1; i++) {
        br.readUe();  // bit_rate_value_minus1
        br.readUe();  // cpb_size_value_minus1
        br.readBit(); // cbr_flag
    }
    br.readBits(5); // initial_cpb_removal_delay_length_minus1
    br.readBits(5); // cpb_removal_delay_length_minus1
    br.readBits(5); // dpb_output_delay_length_minus1
    br.readBits(5); // time_offset_length
}

static void parseVui(BitReader &br, H264SPS &out) {
    out.aspect_ratio_info_present_flag = br.readBit();
    if (out.aspect_ratio_info_present_flag) {
        out.aspect_ratio_idc = (uint8_t)br.readBits(8);
        if (out.aspect_ratio_idc == 255) {
            br.readBits(16); // sar_width
            br.readBits(16); // sar_height
        }
    }
    if (br.readBit()) br.readBit(); // overscan_info_present_flag, overscan_appropriate_flag
    out.video_signal_type_present_flag = br.readBit();
    if (out.video_signal_type_present_flag) {
        out.video_format = (uint8_t)br.readBits(3);
        out.video_full_range_flag = br.readBit();
        if (br.readBit()) { // colour_description_present_flag
            br.readBits(8); // colour_primaries
            br.readBits(8); // transfer_characteristics
            br.readBits(8); // matrix_coefficients
        }
    }
    if (br.readBit()) { // chroma_loc_info_present_flag
        br.readUe();    // chroma_sample_loc_type_top_field
        br.readUe();    // chroma_sample_loc_type_bottom_field
    }
    out.timing_info_present_flag = br.readBit();
    if (out.timing_info_present_flag) {
        out.num_units_in_tick = (uint32_t)br.readBits(32);
        out.time_scale = (uint32_t)br.readBits(32);
        out.fixed_frame_rate_flag = br.readBit();
    }
    out.nal_hrd_parameters_present_flag = br.readBit();
    if (out.nal_hrd_parameters_present_flag) skipHrdParameters(br);
    out.vcl_hrd_parameters_present_flag = br.readBit();
    if (out.vcl_hrd_parameters_present_flag) skipHrdParameters(br);
    if (out.nal_hrd_parameters_present_flag || out.vcl_hrd_parameters_present_flag)
        br.readBit(); // low_delay_hrd_flag
    br.readBit();     // pic_struct_present_flag
    out.bitstream_restriction_flag = br.readBit();
    if (out.bitstream_restriction_flag) {
        br.readBit(); // motion_vectors_over_pic_boundaries_flag
        br.readUe();  // max_bytes_per_pic_denom
        br.readUe();  // max_bits_per_mb_denom
        br.readUe();  // log2_max_mv_length_horizontal
        br.readUe();  // log2_max_mv_length_vertical
        out.max_num_reorder_frames = (uint32_t)br.readUe();
        out.max_dec_frame_buffering = (uint32_t)br.readUe();
    }
}

bool parseH264SPS(const NALUnit &nal, const uint8_t *data, H264SPS &out) {
    if (nal.size < nal.startCodeLen + 2) return false;

    const size_t rbspOffset = nal.startCodeLen + 1;
    BitReader br(data + rbspOffset, nal.size - rbspOffset);

    out.profile_idc = (uint8_t)br.readBits(8);
    out.constraint_set0_flag = br.readBit();
    out.constraint_set1_flag = br.readBit();
    out.constraint_set2_flag = br.readBit();
    out.constraint_set3_flag = br.readBit();
    out.constraint_set4_flag = br.readBit();
    out.constraint_set5_flag = br.readBit();
    br.readBits(2); // reserved_zero_2bits
    out.level_idc = (uint8_t)br.readBits(8);
    out.sps_id = (uint8_t)br.readUe();

    if (hasChromaInfo(out.profile_idc)) {
        out.chroma_format_idc = (uint32_t)br.readUe();
        if (out.chroma_format_idc == 3) out.separate_colour_plane_flag = br.readBit();
        out.bit_depth_luma_minus8 = (uint32_t)br.readUe();
        out.bit_depth_chroma_minus8 = (uint32_t)br.readUe();
        out.qpprime_y_zero_transform_bypass_flag = br.readBit();
        out.seq_scaling_matrix_present_flag = br.readBit();
        if (out.seq_scaling_matrix_present_flag) {
            const int numLists = (out.chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < numLists; i++) {
                if (br.readBit()) skipScalingList(br, (i < 6) ? 16 : 64);
            }
        }
    }

    out.log2_max_frame_num_minus4 = (uint32_t)br.readUe();
    out.pic_order_cnt_type = (uint32_t)br.readUe();

    if (out.pic_order_cnt_type == 0) {
        out.log2_max_pic_order_cnt_lsb_minus4 = (uint32_t)br.readUe();
    } else if (out.pic_order_cnt_type == 1) {
        out.delta_pic_order_always_zero_flag = br.readBit();
        out.offset_for_non_ref_pic = (int32_t)br.readSe();
        out.offset_for_top_to_bottom_field = (int32_t)br.readSe();
        out.num_ref_frames_in_pic_order_cnt_cycle = (uint32_t)br.readUe();
        for (uint32_t i = 0; i < out.num_ref_frames_in_pic_order_cnt_cycle; i++) br.readSe();
    }

    out.max_num_ref_frames = (uint32_t)br.readUe();
    out.gaps_in_frame_num_value_allowed_flag = br.readBit();
    const uint32_t widthInMbs = (uint32_t)br.readUe();
    const uint32_t heightInMapUnits = (uint32_t)br.readUe();

    out.frame_mbs_only_flag = br.readBit();
    if (!out.frame_mbs_only_flag) out.mb_adaptive_frame_field_flag = br.readBit();
    out.direct_8x8_inference_flag = br.readBit();

    out.width = (widthInMbs + 1) * 16;
    out.height = (2 - (out.frame_mbs_only_flag ? 1 : 0)) * (heightInMapUnits + 1) * 16;

    out.hasCropping = br.readBit();
    if (out.hasCropping) {
        out.crop_left = (uint32_t)br.readUe();
        out.crop_right = (uint32_t)br.readUe();
        out.crop_top = (uint32_t)br.readUe();
        out.crop_bottom = (uint32_t)br.readUe();

        uint32_t subWidthC = 1, subHeightC = 1;
        if (out.chroma_format_idc == 1) { subWidthC = 2; subHeightC = 2; }
        else if (out.chroma_format_idc == 2) { subWidthC = 2; subHeightC = 1; }

        uint32_t cropUnitX, cropUnitY;
        if (out.chroma_format_idc == 0 || out.separate_colour_plane_flag) {
            cropUnitX = 1;
            cropUnitY = 2 - (out.frame_mbs_only_flag ? 1 : 0);
        } else {
            cropUnitX = subWidthC;
            cropUnitY = subHeightC * (2 - (out.frame_mbs_only_flag ? 1 : 0));
        }
        out.width -= cropUnitX * (out.crop_left + out.crop_right);
        out.height -= cropUnitY * (out.crop_top + out.crop_bottom);
    }

    out.vui_present_flag = br.readBit();
    if (out.vui_present_flag) parseVui(br, out);
    return true;
}

bool parseH264PPS(const NALUnit &nal, const uint8_t *data, H264PPS &out) {
    if (nal.size < nal.startCodeLen + 2) return false;

    const size_t rbspOffset = nal.startCodeLen + 1;
    BitReader br(data + rbspOffset, nal.size - rbspOffset);

    out.pps_id = (uint8_t)br.readUe();
    out.sps_id = (uint8_t)br.readUe();
    out.entropy_coding_mode_flag = br.readBit();
    out.bottom_field_pic_order_in_frame_present_flag = br.readBit();
    out.num_slice_groups_minus1 = (uint32_t)br.readUe();
    if (out.num_slice_groups_minus1 > 0) {
        out.slice_group_map_type = (uint32_t)br.readUe();
        const uint32_t groups = out.num_slice_groups_minus1 + 1;
        if (out.slice_group_map_type == 0) {
            for (uint32_t i = 0; i < groups; i++) br.readUe(); // run_length_minus1
        } else if (out.slice_group_map_type == 2) {
            for (uint32_t i = 0; i < groups; i++) {
                br.readUe(); // top_left
                br.readUe(); // bottom_right
            }
        } else if (out.slice_group_map_type == 3 ||
                   out.slice_group_map_type == 4 ||
                   out.slice_group_map_type == 5) {
            br.readBit(); // slice_group_change_direction_flag
            br.readUe();  // slice_group_change_rate_minus1
        } else if (out.slice_group_map_type == 6) {
            const uint32_t picSizeInMapUnitsMinus1 = (uint32_t)br.readUe();
            int bits = 0;
            while ((1u << bits) < groups) bits++;
            for (uint32_t i = 0; i <= picSizeInMapUnitsMinus1; i++) br.readBits(bits);
        }
    }
    out.num_ref_idx_l0_default_active_minus1 = (uint32_t)br.readUe();
    out.num_ref_idx_l1_default_active_minus1 = (uint32_t)br.readUe();
    out.weighted_pred_flag = br.readBit();
    out.weighted_bipred_idc = (uint8_t)br.readBits(2);
    out.pic_init_qp_minus26 = (int32_t)br.readSe();
    out.pic_init_qs_minus26 = (int32_t)br.readSe();
    out.chroma_qp_index_offset = (int32_t)br.readSe();
    out.deblocking_filter_control_present_flag = br.readBit();
    out.constrained_intra_pred_flag = br.readBit();
    out.redundant_pic_cnt_present_flag = br.readBit();

    if (br.moreRbspData()) {
        out.transform_8x8_mode_flag = br.readBit();
        out.pic_scaling_matrix_present_flag = br.readBit();
        if (out.pic_scaling_matrix_present_flag) {
            const int numLists = 6 + (out.transform_8x8_mode_flag ? 2 : 0); // 4:2:0
            for (int i = 0; i < numLists; i++) {
                if (br.readBit()) skipScalingList(br, (i < 6) ? 16 : 64);
            }
        }
        out.second_chroma_qp_index_offset = (int32_t)br.readSe();
    }
    return true;
}

bool parseH264SliceHeader(const NALUnit &nal, const uint8_t *data, const H264SPS &sps, H264SliceHeader &out) {
    if (nal.size < nal.startCodeLen + 5) return false;
    const size_t rbspOffset = nal.startCodeLen + 1;
    RbspBitReader br(data + rbspOffset, nal.size - rbspOffset);

    out.first_mb_in_slice = (uint32_t)br.readUe();
    out.slice_type = (uint32_t)br.readUe();
    (void)br.readUe(); // pic_parameter_set_id

    const uint32_t frameNumBits = sps.log2_max_frame_num_minus4 + 4;
    out.frame_num = (uint32_t)br.readBits(frameNumBits);

    if (!sps.frame_mbs_only_flag) {
        const bool fieldPicFlag = br.readBit();
        if (fieldPicFlag) br.readBit(); // bottom_field_flag
    }
    if (nal.nalType == 5) (void)br.readUe(); // idr_pic_id

    if (sps.pic_order_cnt_type == 0) {
        const uint32_t pocBits = sps.log2_max_pic_order_cnt_lsb_minus4 + 4;
        out.pic_order_cnt_lsb = (uint32_t)br.readBits(pocBits);
    }
    return true;
}
