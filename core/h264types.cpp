#include "h264types.h"
#include <stdexcept>

std::string H264SPS::profileName() const {
    switch (profile_idc) {
        case 66: return "Baseline";
        case 77: return "Main";
        case 88: return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4";
        default: return "Unknown";
    }
}

bool parseH264SPS(const std::vector<uint8_t> &nalData, H264SPS &out) {
    if (nalData.size() < 4) return false;

    BitReader br(nalData.data() + 1, nalData.size() - 1);

    out.profile_idc = (uint8_t)br.readBits(8);
    out.constraint_set0_flag = br.readBit();
    out.constraint_set1_flag = br.readBit();
    out.constraint_set2_flag = br.readBit();
    out.constraint_set3_flag = br.readBit();
    br.readBits(4);
    out.level_idc = (uint8_t)br.readBits(8);
    out.sps_id = (uint8_t)br.readUe();

    if (out.profile_idc == 100 || out.profile_idc == 110 ||
        out.profile_idc == 122 || out.profile_idc == 244) {
        uint32_t chroma_format = (uint32_t)br.readUe();
        if (chroma_format == 3) br.readBits(1);
        br.readUe();
        br.readUe();
    }

    out.log2_max_frame_num_minus4 = (uint32_t)br.readUe();
    out.pic_order_cnt_type = (uint32_t)br.readUe();

    if (out.pic_order_cnt_type == 0) {
        br.readUe();
    } else if (out.pic_order_cnt_type == 1) {
        br.readBits(1);
        br.readSe();
        br.readSe();
        uint32_t numRefFramesInPicOrderCntCycle = (uint32_t)br.readUe();
        for (uint32_t i = 0; i < numRefFramesInPicOrderCntCycle; i++) {
            br.readSe();
        }
    }

    out.max_num_ref_frames = (uint32_t)br.readUe();
    br.readBits(1);
    uint32_t log2_minus8 = (uint32_t)br.readUe();
    uint32_t widthInMbs = (uint32_t)br.readUe();
    uint32_t heightInMapUnits = (uint32_t)br.readUe();

    bool frameMbsOnlyFlag = br.readBit();
    if (!frameMbsOnlyFlag) br.readBit();

    out.width = (widthInMbs + 1) * 16;
    out.height = (2 - (frameMbsOnlyFlag ? 1 : 0)) * (heightInMapUnits + 1) * 16;

    out.hasCropping = br.readBit();
    if (out.hasCropping) {
        out.crop_left = (uint32_t)br.readUe();
        out.crop_right = (uint32_t)br.readUe();
        out.crop_top = (uint32_t)br.readUe();
        out.crop_bottom = (uint32_t)br.readUe();
        out.width -= (out.crop_left + out.crop_right) * 2;
        out.height -= (out.crop_top + out.crop_bottom) * 2;
    }

    br.readBit(); // vui_present_flag
    return true;
}

bool parseH264PPS(const std::vector<uint8_t> &nalData, H264PPS &out) {
    if (nalData.size() < 2) return false;

    BitReader br(nalData.data() + 1, nalData.size() - 1);

    out.pps_id = (uint8_t)br.readUe();
    out.sps_id = (uint8_t)br.readUe();
    out.entropy_coding_mode_flag = br.readBit();
    out.bottom_field_pic_order_in_frame_present_flag = br.readBit();
    out.num_slice_groups_minus1 = (uint32_t)br.readUe();

    out.num_ref_idx_l0_default_active_minus1 = (uint32_t)br.readUe();
    out.num_ref_idx_l1_default_active_minus1 = (uint32_t)br.readUe();
    out.weighted_pred_flag = br.readBit();
    out.weighted_bipred_idc = (uint8_t)br.readBits(2);
    out.pic_init_qp_minus26 = (int32_t)br.readSe();
    out.pic_init_qs_minus26 = (int32_t)br.readSe();
    out.chroma_qp_index_offset_present_flag = br.readBit();
    if (out.chroma_qp_index_offset_present_flag) {
        out.chroma_qp_index_offset = (int32_t)br.readSe();
    }
    out.deblocking_filter_control_present_flag = br.readBit();
    out.constrained_intra_pred_flag = br.readBit();
    out.redundant_pic_cnt_present_flag = br.readBit();

    return true;
}