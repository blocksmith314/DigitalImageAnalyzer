#pragma once

#include "expgolomb.h"
#include <cstdint>
#include <string>
#include <vector>

struct H265VPS {
    uint8_t vps_video_parameter_set_id = 0;
    uint8_t base_layer_internal_constraint_info_flag = 0;
    uint8_t max_layers_minus1 = 0;
    uint8_t max_sub_layers_minus1 = 0;
    uint8_t temporal_id_nesting_flag = 0;
};

struct H265SPS {
    uint8_t vps_id = 0;
    uint8_t max_sub_layers_minus1 = 0;
    uint8_t temporal_id_nesting_flag = 0;
    uint16_t general_profile_idc = 0;
    uint16_t general_level_idc = 0;
    uint8_t chroma_format_idc = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pic_width_in_luma_samples = 0;
    uint32_t pic_height_in_luma_samples = 0;
    uint32_t log2_min_luma_coding_block_size_minus3 = 0;
    uint32_t log2_diff_max_min_luma_coding_block_size = 0;
    uint32_t log2_min_transform_block_size_minus2 = 0;
    uint32_t log2_diff_max_min_transform_block_size = 0;
    uint32_t max_transform_hierarchy_depth_inter = 0;
    uint32_t max_transform_hierarchy_depth_intra = 0;
    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
    bool hasCropping = false;
    uint32_t num_ref_pic_lists_in_sps = 0;
    uint32_t max_num_ref_pics = 0;
    uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0;
    std::string profileName() const;
};

struct H265PPS {
    uint8_t pps_id = 0;
    uint8_t sps_id = 0;
    uint8_t max_num_ref_idx_l0_default_active_minus1 = 0;
    uint8_t max_num_ref_idx_l1_default_active_minus1 = 0;
    uint8_t init_qp_minus26 = 0;
    bool constrained_intra_pred_flag = false;
    bool cu_qp_delta_enabled_flag = false;
    uint32_t diff_cu_qp_delta_depth = 0;
    bool pps_deblocking_filter_disabled_flag = false;
};

bool parseH265VPS(const std::vector<uint8_t> &nalData, H265VPS &out);
bool parseH265SPS(const std::vector<uint8_t> &nalData, H265SPS &out);
bool parseH265PPS(const std::vector<uint8_t> &nalData, H265PPS &out);