#pragma once

#include "expgolomb.h"
#include "nalunit.h"
#include <cstdint>
#include <string>
#include <vector>

struct H264SPS {
    uint8_t profile_idc = 0;
    uint8_t level_idc = 0;
    uint8_t sps_id = 0;
    bool constraint_set0_flag = false;
    bool constraint_set1_flag = false;
    bool constraint_set2_flag = false;
    bool constraint_set3_flag = false;
    uint32_t log2_max_frame_num_minus4 = 0;
    uint32_t pic_order_cnt_type = 0;
    uint32_t max_num_ref_frames = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t crop_left = 0;
    uint32_t crop_right = 0;
    uint32_t crop_top = 0;
    uint32_t crop_bottom = 0;
    bool hasCropping = false;
    std::string profileName() const;
};

struct H264PPS {
    uint8_t pps_id = 0;
    uint8_t sps_id = 0;
    bool entropy_coding_mode_flag = false;
    bool bottom_field_pic_order_in_frame_present_flag = false;
    uint32_t num_slice_groups_minus1 = 0;
    uint32_t num_ref_idx_l0_default_active_minus1 = 0;
    uint32_t num_ref_idx_l1_default_active_minus1 = 0;
    bool weighted_pred_flag = false;
    uint8_t weighted_bipred_idc = 0;
    int32_t pic_init_qp_minus26 = 0;
    int32_t pic_init_qs_minus26 = 0;
    bool chroma_qp_index_offset_present_flag = false;
    int32_t chroma_qp_index_offset = 0;
    bool deblocking_filter_control_present_flag = false;
    bool constrained_intra_pred_flag = false;
    bool redundant_pic_cnt_present_flag = false;
};

bool parseH264SPS(const NALUnit &nal, H264SPS &out);
bool parseH264PPS(const NALUnit &nal, H264PPS &out);

struct H264SliceHeader {
    uint32_t first_mb_in_slice = 0;
    uint32_t slice_type = 0;
    bool isIFrame() const { return slice_type % 5 == 2; }
    bool isBFrame() const { return slice_type % 5 == 1; }
    bool isPFrame() const { return slice_type % 5 == 0 || slice_type % 5 == 3 || slice_type % 5 == 4; }
    char frameTypeChar() const {
        if (isIFrame()) return 'I';
        if (isBFrame()) return 'B';
        return 'P';
    }
};

bool parseH264SliceHeader(const NALUnit &nal, const H264SPS &sps, H264SliceHeader &out);