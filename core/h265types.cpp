#include "h265types.h"
#include <algorithm>

std::string H265SPS::profileName() const {
    switch (profile_idc) {
        case 1: return "Main";
        case 2: return "Main 10";
        case 3: return "Main Still Picture";
        case 4: return "Main 4:2:2 10";
        case 5: return "Main 4:4:4 10";
        case 6: return "Main 12";
        case 7: return "Main 4:2:2 12";
        case 8: return "Main 4:4:4 12";
        case 9: return "Main 4:4:4 16 Intra";
        default: return "Unknown";
    }
}

namespace {

void skipSubLayerProfile(BitReader &br) {
    br.readBits(2);  // profile_space
    br.readBits(1);  // tier_flag
    br.readBits(5);  // profile_idc
    for (int i = 0; i < 32; i++) br.readBit(); // compatibility flags
    br.readBits(4);  // progressive / interlaced / non_packed / frame_only
    br.readBits(43); // constraint flags
    br.readBits(1);  // inbld
}

void skipSubLayerPtl(BitReader &br, uint8_t maxSubLayersMinus1) {
    uint8_t subProfile[8] = {0};
    uint8_t subLevel[8] = {0};
    for (int i = 0; i < maxSubLayersMinus1; i++) {
        subProfile[i] = br.readBit();
        subLevel[i] = br.readBit();
    }
    if (maxSubLayersMinus1 > 0) {
        for (int i = maxSubLayersMinus1; i < 8; i++) br.readBits(2);
    }
    for (int i = 0; i < maxSubLayersMinus1; i++) {
        if (subProfile[i]) skipSubLayerProfile(br);
        if (subLevel[i]) br.readBits(8); // sub_layer_level_idc
    }
}

void skipGeneralPtlFields(BitReader &br) {
    br.readBits(2); // profile_space
    br.readBits(1); // tier_flag
    br.readBits(5); // profile_idc
    for (int i = 0; i < 32; i++) br.readBit(); // compatibility flags
    br.readBits(4);  // progressive / interlaced / non_packed / frame_only
    br.readBits(43); // constraint flags
    br.readBits(1);  // inbld
    br.readBits(8);  // level_idc
}

void parseGeneralPtl(BitReader &br, H265SPS &out) {
    out.profile_space = (uint8_t)br.readBits(2);
    out.tier_flag = br.readBit();
    out.profile_idc = (uint8_t)br.readBits(5);
    for (int i = 0; i < 32; i++) br.readBit(); // general_profile_compatibility_flags
    out.progressive_source_flag = br.readBit();
    out.interlaced_source_flag = br.readBit();
    out.non_packed_constraint_flag = br.readBit();
    out.frame_only_constraint_flag = br.readBit();
    br.readBits(43); // constraint flags
    br.readBits(1);  // general_inbld_flag
    out.level_idc = (uint8_t)br.readBits(8);
    skipSubLayerPtl(br, out.max_sub_layers_minus1);
}

void skipScalingListData(BitReader &br) {
    for (int sizeId = 0; sizeId < 4; sizeId++) {
        for (int matrixId = 0; matrixId < 6; matrixId += (sizeId == 3) ? 3 : 1) {
            const bool predMode = br.readBit();
            if (!predMode) {
                br.readUe(); // scaling_list_pred_matrix_id_delta
            } else {
                const int coefNum = std::min(64, 1 << (4 + (sizeId << 1)));
                if (sizeId > 1) br.readSe(); // scaling_list_dc_coef_minus8
                for (int i = 0; i < coefNum; i++) br.readSe();
            }
        }
    }
}

void skipHrdParameters(BitReader &br, uint8_t maxSubLayersMinus1) {
    const bool nalHrd = br.readBit();
    const bool vclHrd = br.readBit();
    bool subPic = false;
    if (nalHrd || vclHrd) {
        subPic = br.readBit();
        if (subPic) {
            br.readBits(8); // tick_divisor_minus2
            br.readBits(5); // du_cpb_removal_delay_increment_length_minus1
            br.readBit();   // sub_pic_cpb_params_in_pic_timing_sei_flag
            br.readBits(5); // dpb_output_delay_du_length_minus1
        }
        br.readBits(4); // bit_rate_scale
        br.readBits(4); // cpb_size_scale
        if (subPic) br.readBits(4); // cpb_size_du_scale
        br.readBits(5); // initial_cpb_removal_delay_length_minus1
        br.readBits(5); // au_cpb_removal_delay_length_minus1
        br.readBits(5); // dpb_output_delay_length_minus1
    }
    for (int i = 0; i <= maxSubLayersMinus1; i++) {
        const bool fixedGeneral = br.readBit();
        bool fixedWithin = fixedGeneral;
        if (!fixedGeneral) fixedWithin = br.readBit();
        bool lowDelay = false;
        if (fixedWithin) {
            br.readUe(); // elemental_duration_in_tc_minus1
        } else {
            lowDelay = br.readBit();
        }
        uint32_t cpbCntMinus1 = 0;
        if (!lowDelay) cpbCntMinus1 = (uint32_t)br.readUe();
        for (int pass = 0; pass < 2; pass++) {
            if (pass == 0 && !nalHrd) continue;
            if (pass == 1 && !vclHrd) continue;
            for (uint32_t j = 0; j <= cpbCntMinus1; j++) {
                br.readUe(); // bit_rate_value_minus1
                br.readUe(); // cpb_size_value_minus1
                if (subPic) {
                    br.readUe(); // cpb_size_du_value_minus1
                    br.readUe(); // bit_rate_du_value_minus1
                }
                br.readBit(); // cbr_flag
            }
        }
    }
}

void parseVui(BitReader &br, H265SPS &out) {
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
            br.readBits(8); // matrix_coeffs
        }
    }
    if (br.readBit()) { // chroma_loc_info_present_flag
        br.readUe();    // chroma_sample_loc_type_top_field
        br.readUe();    // chroma_sample_loc_type_bottom_field
    }
    br.readBit(); // neutral_chroma_indication_flag
    br.readBit(); // field_seq_flag
    br.readBit(); // frame_field_info_present_flag
    if (br.readBit()) { // default_display_window_flag
        br.readUe();    // def_disp_win_left_offset
        br.readUe();    // def_disp_win_right_offset
        br.readUe();    // def_disp_win_top_offset
        br.readUe();    // def_disp_win_bottom_offset
    }
    out.vui_timing_info_present_flag = br.readBit();
    if (out.vui_timing_info_present_flag) {
        out.vui_num_units_in_tick = (uint32_t)br.readBits(32);
        out.vui_time_scale = (uint32_t)br.readBits(32);
        if (br.readBit()) br.readUe(); // vui_poc_proportional_to_timing_flag
        if (br.readBit()) skipHrdParameters(br, out.max_sub_layers_minus1);
    }
    out.bitstream_restriction_flag = br.readBit();
    if (out.bitstream_restriction_flag) {
        br.readBit(); // tiles_fixed_structure_flag
        br.readBit(); // motion_vectors_over_pic_boundaries_flag
        br.readBit(); // restricted_ref_pic_lists_flag
        br.readUe();  // min_spatial_segmentation_idc
        br.readUe();  // max_bytes_per_pic_denom
        br.readUe();  // max_bits_per_min_cu_denom
        br.readUe();  // log2_max_mv_length_horizontal
        br.readUe();  // log2_max_mv_length_vertical
    }
}

struct ShortTermRps {
    std::vector<int> pocS0;
    std::vector<int> pocS1;
    std::vector<bool> usedS0;
    std::vector<bool> usedS1;
    uint32_t numDeltaPocs() const {
        return (uint32_t)(pocS0.size() + pocS1.size());
    }
};

void parseShortTermRefPicSet(BitReader &br, uint32_t idx,
                             const std::vector<ShortTermRps> &rpsList,
                             std::vector<ShortTermRps> &out) {
    ShortTermRps rps;
    if (idx != 0) {
        const bool interPred = br.readBit();
        if (interPred) {
            const uint32_t refIdx = idx - 1;
            if (refIdx >= rpsList.size()) return; // malformed; stop parsing this set
            const ShortTermRps &ref = rpsList[refIdx];
            const bool deltaRpsSign = br.readBit();
            const uint32_t absDeltaRpsMinus1 = (uint32_t)br.readUe();
            const int deltaRps = (1 - 2 * (int)deltaRpsSign) * (int)(absDeltaRpsMinus1 + 1);

            const uint32_t numDelta = ref.numDeltaPocs();
            std::vector<bool> used(numDelta + 1, false);
            std::vector<bool> useDelta(numDelta + 1, true);
            for (uint32_t j = 0; j <= numDelta; j++) {
                used[j] = br.readBit();
                if (!used[j]) useDelta[j] = br.readBit();
            }

            const int numNegRef = (int)ref.pocS0.size();
            const int numPosRef = (int)ref.pocS1.size();

            for (int j = numPosRef - 1; j >= 0; j--) {
                const int dPoc = ref.pocS1[j] + deltaRps;
                const int flagIdx = numNegRef + j;
                if (dPoc < 0 && useDelta[flagIdx]) {
                    rps.pocS0.push_back(dPoc);
                    rps.usedS0.push_back(used[flagIdx]);
                } else if (dPoc > 0) {
                    rps.pocS1.push_back(dPoc);
                    rps.usedS1.push_back(used[flagIdx]);
                }
            }
            for (int j = 0; j < numNegRef; j++) {
                const int dPoc = ref.pocS0[j] + deltaRps;
                if (dPoc < 0 && useDelta[j]) {
                    rps.pocS0.push_back(dPoc);
                    rps.usedS0.push_back(used[j]);
                } else if (dPoc > 0) {
                    rps.pocS1.push_back(dPoc);
                    rps.usedS1.push_back(used[j]);
                }
            }
            if (deltaRps < 0 && useDelta[numDelta]) {
                rps.pocS0.push_back(deltaRps);
                rps.usedS0.push_back(used[numDelta]);
            } else if (deltaRps > 0) {
                rps.pocS1.push_back(deltaRps);
                rps.usedS1.push_back(used[numDelta]);
            }

            out.push_back(std::move(rps));
            return;
        }
    }

    const uint32_t numNeg = (uint32_t)br.readUe();
    const uint32_t numPos = (uint32_t)br.readUe();
    int prev = 0;
    for (uint32_t i = 0; i < numNeg; i++) {
        prev -= (int)br.readUe() + 1;
        rps.pocS0.push_back(prev);
        rps.usedS0.push_back(br.readBit());
    }
    prev = 0;
    for (uint32_t i = 0; i < numPos; i++) {
        prev += (int)br.readUe() + 1;
        rps.pocS1.push_back(prev);
        rps.usedS1.push_back(br.readBit());
    }
    out.push_back(std::move(rps));
}

} // namespace

bool parseH265VPS(const NALUnit &nal, H265VPS &out) {
    if (nal.data.size() < nal.startCodeLen + 3) return false;

    const size_t rbspOffset = nal.startCodeLen + 2;
    BitReader br(nal.data.data() + rbspOffset, nal.data.size() - rbspOffset);

    out.vps_id = (uint8_t)br.readBits(4);
    out.base_layer_internal_flag = br.readBit();
    out.base_layer_available_flag = br.readBit();
    out.max_layers_minus1 = (uint8_t)br.readBits(6);
    out.max_sub_layers_minus1 = (uint8_t)br.readBits(3);
    out.temporal_id_nesting_flag = br.readBit();
    br.readBits(16); // vps_reserved_0xffff_16bits

    skipGeneralPtlFields(br);
    skipSubLayerPtl(br, out.max_sub_layers_minus1);

    const bool subLayerOrderingInfoPresent = br.readBit();
    for (int i = (subLayerOrderingInfoPresent ? 0 : out.max_sub_layers_minus1);
         i <= out.max_sub_layers_minus1; i++) {
        br.readUe(); // vps_max_dec_pic_buffering_minus1
        br.readUe(); // vps_max_num_reorder_pics
        br.readUe(); // vps_max_latency_increase_plus1
    }
    out.max_layer_id = (uint8_t)br.readBits(6);
    return true;
}

bool parseH265SPS(const NALUnit &nal, H265SPS &out) {
    if (nal.data.size() < nal.startCodeLen + 3) return false;

    const size_t rbspOffset = nal.startCodeLen + 2;
    BitReader br(nal.data.data() + rbspOffset, nal.data.size() - rbspOffset);

    out.vps_id = (uint8_t)br.readBits(4);
    out.max_sub_layers_minus1 = (uint8_t)br.readBits(3);
    out.temporal_id_nesting_flag = br.readBit();
    parseGeneralPtl(br, out);

    out.sps_id = (uint8_t)br.readUe();
    out.chroma_format_idc = (uint8_t)br.readUe();
    if (out.chroma_format_idc == 3) out.separate_colour_plane_flag = br.readBit();

    out.pic_width_in_luma_samples = (uint32_t)br.readUe();
    out.pic_height_in_luma_samples = (uint32_t)br.readUe();
    out.hasCropping = br.readBit(); // conformance_window_flag
    if (out.hasCropping) {
        out.crop_left = (uint32_t)br.readUe();
        out.crop_right = (uint32_t)br.readUe();
        out.crop_top = (uint32_t)br.readUe();
        out.crop_bottom = (uint32_t)br.readUe();
    }

    out.width = out.pic_width_in_luma_samples;
    out.height = out.pic_height_in_luma_samples;
    if (out.hasCropping) {
        uint32_t subWidthC = 1, subHeightC = 1;
        if (out.chroma_format_idc == 1) { subWidthC = 2; subHeightC = 2; }
        else if (out.chroma_format_idc == 2) { subWidthC = 2; subHeightC = 1; }
        out.width -= subWidthC * (out.crop_left + out.crop_right);
        out.height -= subHeightC * (out.crop_top + out.crop_bottom);
    }

    out.bit_depth_luma_minus8 = (uint32_t)br.readUe();
    out.bit_depth_chroma_minus8 = (uint32_t)br.readUe();
    out.log2_max_pic_order_cnt_lsb_minus4 = (uint32_t)br.readUe();

    out.sub_layer_ordering_info_present_flag = br.readBit();
    for (int i = (out.sub_layer_ordering_info_present_flag ? 0 : out.max_sub_layers_minus1);
         i <= out.max_sub_layers_minus1; i++) {
        out.max_dec_pic_buffering_minus1 = (uint32_t)br.readUe();
        out.max_num_reorder_pics = (uint32_t)br.readUe();
        out.max_latency_increase_plus1 = (uint32_t)br.readUe();
    }

    out.log2_min_luma_coding_block_size_minus3 = (uint32_t)br.readUe();
    out.log2_diff_max_min_luma_coding_block_size = (uint32_t)br.readUe();
    out.log2_min_transform_block_size_minus2 = (uint32_t)br.readUe();
    out.log2_diff_max_min_transform_block_size = (uint32_t)br.readUe();
    out.max_transform_hierarchy_depth_inter = (uint32_t)br.readUe();
    out.max_transform_hierarchy_depth_intra = (uint32_t)br.readUe();

    out.scaling_list_enabled_flag = br.readBit();
    if (out.scaling_list_enabled_flag) {
        const bool scalingListDataPresent = br.readBit();
        if (scalingListDataPresent) skipScalingListData(br);
    }
    out.amp_enabled_flag = br.readBit();
    out.sample_adaptive_offset_enabled_flag = br.readBit();
    out.pcm_enabled_flag = br.readBit();
    if (out.pcm_enabled_flag) {
        br.readBits(4); // pcm_sample_bit_depth_luma_minus1
        br.readBits(4); // pcm_sample_bit_depth_chroma_minus1
        br.readUe();    // log2_min_pcm_luma_coding_block_size_minus3
        br.readUe();    // log2_diff_max_min_pcm_luma_coding_block_size
        br.readBit();   // pcm_loop_filter_disabled_flag
    }

    out.num_short_term_ref_pic_sets = (uint32_t)br.readUe();
    std::vector<ShortTermRps> rpsList;
    for (uint32_t i = 0; i < out.num_short_term_ref_pic_sets; i++)
        parseShortTermRefPicSet(br, i, rpsList, rpsList);

    out.long_term_ref_pics_present_flag = br.readBit();
    if (out.long_term_ref_pics_present_flag) {
        out.num_long_term_ref_pics_sps = (uint32_t)br.readUe();
        const int pocBits = (int)out.log2_max_pic_order_cnt_lsb_minus4 + 4;
        for (uint32_t i = 0; i < out.num_long_term_ref_pics_sps; i++) {
            br.readBits(pocBits); // lt_ref_pic_poc_lsb_sps
            br.readBit();         // used_by_curr_pic_lt_sps_flag
        }
    }
    out.sps_temporal_mvp_enabled_flag = br.readBit();
    out.strong_intra_smoothing_enabled_flag = br.readBit();
    out.vui_present_flag = br.readBit();
    if (out.vui_present_flag) parseVui(br, out);
    out.sps_extension_present_flag = br.readBit();
    return true;
}

bool parseH265PPS(const NALUnit &nal, H265PPS &out) {
    if (nal.data.size() < nal.startCodeLen + 3) return false;

    const size_t rbspOffset = nal.startCodeLen + 2;
    BitReader br(nal.data.data() + rbspOffset, nal.data.size() - rbspOffset);

    out.pps_id = (uint8_t)br.readUe();
    out.sps_id = (uint8_t)br.readUe();
    out.dependent_slice_segments_enabled_flag = br.readBit();
    out.output_flag_present_flag = br.readBit();
    out.num_extra_slice_header_bits = (uint8_t)br.readBits(3);
    out.sign_data_hiding_enabled_flag = br.readBit();
    out.cabac_init_present_flag = br.readBit();
    out.num_ref_idx_l0_default_active_minus1 = (uint32_t)br.readUe();
    out.num_ref_idx_l1_default_active_minus1 = (uint32_t)br.readUe();
    out.init_qp_minus26 = (int32_t)br.readSe();
    out.constrained_intra_pred_flag = br.readBit();
    out.transform_skip_enabled_flag = br.readBit();
    out.cu_qp_delta_enabled_flag = br.readBit();
    if (out.cu_qp_delta_enabled_flag) out.diff_cu_qp_delta_depth = (uint32_t)br.readUe();
    out.pps_cb_qp_offset = (int32_t)br.readSe();
    out.pps_cr_qp_offset = (int32_t)br.readSe();
    out.pps_slice_chroma_qp_offsets_present_flag = br.readBit();
    out.weighted_pred_flag = br.readBit();
    out.weighted_bipred_flag = br.readBit();
    out.transquant_bypass_enabled_flag = br.readBit();
    out.tiles_enabled_flag = br.readBit();
    out.entropy_coding_sync_enabled_flag = br.readBit();
    if (out.tiles_enabled_flag) {
        out.num_tile_columns_minus1 = (uint32_t)br.readUe();
        out.num_tile_rows_minus1 = (uint32_t)br.readUe();
        out.uniform_spacing_flag = br.readBit();
        if (!out.uniform_spacing_flag) {
            for (uint32_t i = 0; i < out.num_tile_columns_minus1; i++) br.readUe();
            for (uint32_t i = 0; i < out.num_tile_rows_minus1; i++) br.readUe();
        }
        br.readBit(); // loop_filter_across_tiles_enabled_flag
    }
    out.pps_loop_filter_across_slices_enabled_flag = br.readBit();
    out.deblocking_filter_control_present_flag = br.readBit();
    if (out.deblocking_filter_control_present_flag) {
        out.deblocking_filter_override_enabled_flag = br.readBit();
        out.pps_deblocking_filter_disabled_flag = br.readBit();
        if (!out.pps_deblocking_filter_disabled_flag) {
            out.pps_beta_offset_div2 = (int32_t)br.readSe();
            out.pps_tc_offset_div2 = (int32_t)br.readSe();
        }
    }
    out.pps_scaling_list_data_present_flag = br.readBit();
    if (out.pps_scaling_list_data_present_flag) skipScalingListData(br);
    out.lists_modification_present_flag = br.readBit();
    out.log2_parallel_merge_level_minus2 = (uint32_t)br.readUe();
    out.slice_segment_header_extension_present_flag = br.readBit();
    out.pps_extension_present_flag = br.readBit();
    return true;
}
