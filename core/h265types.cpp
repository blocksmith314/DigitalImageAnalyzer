#include "h265types.h"
#include <stdexcept>

std::string H265SPS::profileName() const {
    switch (general_profile_idc) {
        case 1: return "Main";
        case 2: return "Main 10";
        case 3: return "Main Still Picture";
        case 4: return "Main 4:2:2 10";
        case 5: return "Main 4:4:4 10";
        case 6: return "Main 12";
        case 7: return "Main 4:2:2 12";
        case 8: return "Main 4:4:4 12";
        default: return "Unknown";
    }
}

bool parseH265VPS(const std::vector<uint8_t> &nalData, H265VPS &out) {
    if (nalData.size() < 3) return false;
    BitReader br(nalData.data() + 2, nalData.size() - 2);

    out.vps_video_parameter_set_id = (uint8_t)br.readBits(4);
    out.base_layer_internal_constraint_info_flag = (uint8_t)br.readBit();
    br.readBits(2);
    out.max_layers_minus1 = (uint8_t)br.readBits(6);
    out.max_sub_layers_minus1 = (uint8_t)br.readBits(3);
    out.temporal_id_nesting_flag = (uint8_t)br.readBit();

    // 跳过 profile_tier_level
    br.readBits(2);
    br.readBits(32); // general_profile_compatibility_flag
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(1);
    br.readBits(48);
    br.readBits(16);

    return true;
}

bool parseH265SPS(const std::vector<uint8_t> &nalData, H265SPS &out) {
    if (nalData.size() < 3) return false;
    BitReader br(nalData.data() + 2, nalData.size() - 2);

    out.vps_id = (uint8_t)br.readBits(4);
    out.max_sub_layers_minus1 = (uint8_t)br.readBits(3);
    out.temporal_id_nesting_flag = (uint8_t)br.readBit();

    // profile_tier_level
    br.readBits(2);
    out.general_profile_idc = (uint16_t)br.readBits(7);
    for (int i = 0; i < 32; i++) br.readBit(); // compatibility flags
    br.readBits(1); br.readBits(1); br.readBits(1); br.readBits(1);
    br.readBits(1); br.readBits(1); br.readBits(1); br.readBits(1);
    br.readBits(48); // reserved
    out.general_level_idc = (uint16_t)br.readBits(8);

    out.chroma_format_idc = (uint8_t)br.readUe();
    if (out.chroma_format_idc == 3) br.readBit(); // separate_colour_plane_flag

    out.pic_width_in_luma_samples = (uint32_t)br.readUe();
    out.pic_height_in_luma_samples = (uint32_t)br.readUe();
    out.width = out.pic_width_in_luma_samples;
    out.height = out.pic_height_in_luma_samples;

    out.log2_min_luma_coding_block_size_minus3 = (uint32_t)br.readUe();
    out.log2_diff_max_min_luma_coding_block_size = (uint32_t)br.readUe();
    out.log2_min_transform_block_size_minus2 = (uint32_t)br.readUe();
    out.log2_diff_max_min_transform_block_size = (uint32_t)br.readUe();
    out.max_transform_hierarchy_depth_inter = (uint32_t)br.readUe();
    out.max_transform_hierarchy_depth_intra = (uint32_t)br.readUe();

    // scaling_list_data — 简化跳过
    bool scaling_list_enable_flag = br.readBit();
    if (scaling_list_enable_flag) {
        // 简化：跳过整个 scaling_list_data
        // 这部分比较复杂，实际工程里需要完整实现
    }

    br.readBits(1);
    br.readBits(1);
    bool pcmEnabled = br.readBit();
    if (pcmEnabled) {
        br.readUe(); br.readUe(); br.readBits(1); br.readBits(8); br.readBits(8);
    }

    out.max_num_ref_pics = (uint32_t)br.readUe();
    bool sublayer_cp_flag_present = br.readBit();
    if (sublayer_cp_flag_present) {
        for (int i = 0; i <= out.max_sub_layers_minus1; i++) br.readBit();
    }
    out.num_ref_pic_lists_in_sps = (uint32_t)br.readUe();
    out.log2_max_pic_order_cnt_lsb_minus4 = (uint32_t)br.readUe();

    // 继续读 crop
    out.hasCropping = br.readBit(); // pic_cropping_flag
    if (out.hasCropping) {
        out.crop_left = (uint32_t)br.readUe();
        out.crop_right = (uint32_t)br.readUe();
        out.crop_top = (uint32_t)br.readUe();
        out.crop_bottom = (uint32_t)br.readUe();
        uint32_t subWidthC = out.chroma_format_idc == 1 || out.chroma_format_idc == 2 ? 2 : 1;
        uint32_t subHeightC = out.chroma_format_idc == 1 ? 2 : 1;
        out.width -= (out.crop_left + out.crop_right) * subWidthC;
        out.height -= (out.crop_top + out.crop_bottom) * subHeightC;
    }

    return true;
}

bool parseH265PPS(const std::vector<uint8_t> &nalData, H265PPS &out) {
    if (nalData.size() < 3) return false;
    BitReader br(nalData.data() + 2, nalData.size() - 2);

    out.pps_id = (uint8_t)br.readUe();
    out.sps_id = (uint8_t)br.readUe();
    bool dependent_slice_segments_enabled_flag = br.readBit();
    out.max_num_ref_idx_l0_default_active_minus1 = (uint8_t)br.readUe();
    out.max_num_ref_idx_l1_default_active_minus1 = (uint8_t)br.readUe();
    out.init_qp_minus26 = (int8_t)br.readSe();
    br.readSe(); // init_qs_minus26
    br.readBits(1); // pps_cb_qp_offset
    br.readBits(1); // pps_cr_qp_offset
    out.constrained_intra_pred_flag = br.readBit();
    br.readBits(1); // transform_skip_enabled_flag
    out.cu_qp_delta_enabled_flag = br.readBit();
    if (out.cu_qp_delta_enabled_flag) out.diff_cu_qp_delta_depth = (uint32_t)br.readUe();
    bool pps_dbf_disabled = br.readBit();
    if (pps_dbf_disabled) out.pps_deblocking_filter_disabled_flag = true;

    return true;
}