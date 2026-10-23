#include "nalanalyzer.h"
#include <sstream>

void NALAnalyzer::analyzeAll(const std::vector<NALUnit> &nals) {
    m_stats = StreamStats();
    m_h264SpsMap.clear();
    m_h264PpsMap.clear();
    m_h265SpsMap.clear();
    m_h265PpsMap.clear();
    m_h265VpsMap.clear();

    if (nals.empty()) return;
    m_stats.codec = nals.front().codec;
    m_stats.totalNals = (uint32_t)nals.size();

    for (const auto &nal : nals) {
        m_stats.totalBytes += nal.size;
        NALAnalysis a = analyzeOne(nal);
        if (a.isSPS) {
            m_stats.spsCount++;
            if (nal.codec == CodecType::H264) {
                m_h264SpsMap[nal.nalType] = a.h264sps;
                if (m_stats.width == 0) {
                    m_stats.width = a.h264sps.width;
                    m_stats.height = a.h264sps.height;
                    m_stats.profile = a.h264sps.profileName();
                    m_stats.level = std::to_string(a.h264sps.level_idc);
                }
            } else {
                m_h265SpsMap[nal.nalType] = a.h265sps;
                if (m_stats.width == 0) {
                    m_stats.width = a.h265sps.width;
                    m_stats.height = a.h265sps.height;
                    m_stats.profile = a.h265sps.profileName();
                    m_stats.level = std::to_string(a.h265sps.general_level_idc);
                }
            }
        } else if (a.isPPS) {
            m_stats.ppsCount++;
            if (nal.codec == CodecType::H264) m_h264PpsMap[nal.nalType] = a.h264pps;
            else m_h265PpsMap[nal.nalType] = a.h265pps;
        } else if (a.isVPS) {
            m_stats.vpsCount++;
            m_h265VpsMap[nal.nalType] = a.h265vps;
        }

        if (nal.codec == CodecType::H264) {
            if (nal.nalType == 5) m_stats.idrCount++;
            if (nal.nalType == 6) m_stats.seiCount++;
            if (nal.nalType == 9) m_stats.audCount++;
            if (nal.nalType >= 1 && nal.nalType <= 5) m_stats.frameCount++;
        } else {
            if (nal.nalType == 19 || nal.nalType == 20 || nal.nalType == 21) m_stats.idrCount++;
            if (nal.nalType == 39 || nal.nalType == 40) m_stats.seiCount++;
            if (nal.nalType == 35) m_stats.audCount++;
            if (nal.nalType >= 0 && nal.nalType <= 21) m_stats.frameCount++;
        }
    }
}

NALAnalysis NALAnalyzer::analyzeOne(const NALUnit &nal) const {
    NALAnalysis a;
    std::ostringstream oss;

    if (nal.codec == CodecType::H264) {
        switch (nal.nalType) {
            case 7: {
                a.isSPS = true;
                if (parseH264SPS(nal.data, a.h264sps)) {
                    oss << "=== H.264 SPS Details ===\n";
                    oss << "Profile: " << a.h264sps.profileName() << " (" << (int)a.h264sps.profile_idc << ")\n";
                    oss << "Level: " << (int)a.h264sps.level_idc << "\n";
                    oss << "SPS ID: " << (int)a.h264sps.sps_id << "\n";
                    oss << "Resolution: " << a.h264sps.width << " x " << a.h264sps.height << "\n";
                    oss << "Max frame num: 2^" << a.h264sps.log2_max_frame_num_minus4 + 4 << "\n";
                    oss << "PIC order cnt type: " << a.h264sps.pic_order_cnt_type << "\n";
                    oss << "Max ref frames: " << a.h264sps.max_num_ref_frames << "\n";
                    oss << "Constraint set 0: " << a.h264sps.constraint_set0_flag << "\n";
                    oss << "Constraint set 1: " << a.h264sps.constraint_set1_flag << "\n";
                    oss << "Constraint set 2: " << a.h264sps.constraint_set2_flag << "\n";
                    oss << "Constraint set 3: " << a.h264sps.constraint_set3_flag << "\n";
                    if (a.h264sps.hasCropping) {
                        oss << "Cropping: left=" << a.h264sps.crop_left
                            << " right=" << a.h264sps.crop_right
                            << " top=" << a.h264sps.crop_top
                            << " bottom=" << a.h264sps.crop_bottom << "\n";
                    }
                } else {
                    oss << "Failed to parse SPS\n";
                }
                break;
            }
            case 8: {
                a.isPPS = true;
                if (parseH264PPS(nal.data, a.h264pps)) {
                    oss << "=== H.264 PPS Details ===\n";
                    oss << "PPS ID: " << (int)a.h264pps.pps_id << "\n";
                    oss << "SPS ID: " << (int)a.h264pps.sps_id << "\n";
                    oss << "Entropy coding (CABAC): " << a.h264pps.entropy_coding_mode_flag << "\n";
                    oss << "Weighted pred: " << a.h264pps.weighted_pred_flag << "\n";
                    oss << "Weighted bipred idc: " << (int)a.h264pps.weighted_bipred_idc << "\n";
                    oss << "Init QP: " << 26 + a.h264pps.pic_init_qp_minus26 << "\n";
                    oss << "Deblocking filter present: " << a.h264pps.deblocking_filter_control_present_flag << "\n";
                } else {
                    oss << "Failed to parse PPS\n";
                }
                break;
            }
            case 5:
                oss << "=== H.264 IDR Slice ===\nType: Instantaneous Decoding Refresh (keyframe)\n";
                break;
            case 1:
                oss << "=== H.264 Non-IDR Slice ===\n";
                break;
            case 6:
                oss << "=== H.264 SEI (Supplemental Enhancement Information) ===\n";
                break;
            case 9:
                oss << "=== H.264 AUD (Access Unit Delimiter) ===\n";
                break;
            default:
                oss << "=== H.264 NAL Type " << (int)nal.nalType << " ===\n";
                break;
        }
    } else {
        switch (nal.nalType) {
            case 32: {
                a.isVPS = true;
                if (parseH265VPS(nal.data, a.h265vps)) {
                    oss << "=== H.265 VPS Details ===\n";
                    oss << "VPS ID: " << (int)a.h265vps.vps_video_parameter_set_id << "\n";
                    oss << "Max layers: " << (int)a.h265vps.max_layers_minus1 + 1 << "\n";
                    oss << "Max sub-layers: " << (int)a.h265vps.max_sub_layers_minus1 + 1 << "\n";
                } else {
                    oss << "Failed to parse VPS\n";
                }
                break;
            }
            case 33: {
                a.isSPS = true;
                if (parseH265SPS(nal.data, a.h265sps)) {
                    oss << "=== H.265 SPS Details ===\n";
                    oss << "VPS ID: " << (int)a.h265sps.vps_id << "\n";
                    oss << "Profile: " << a.h265sps.profileName() << " (" << a.h265sps.general_profile_idc << ")\n";
                    oss << "Level: " << a.h265sps.general_level_idc << "\n";
                    oss << "Resolution: " << a.h265sps.width << " x " << a.h265sps.height << "\n";
                    oss << "Pic width in luma: " << a.h265sps.pic_width_in_luma_samples << "\n";
                    oss << "Pic height in luma: " << a.h265sps.pic_height_in_luma_samples << "\n";
                    oss << "Chroma format: " << (int)a.h265sps.chroma_format_idc << "\n";
                    oss << "Log2(Min luma CB size): " << a.h265sps.log2_min_luma_coding_block_size_minus3 + 3 << "\n";
                    oss << "Log2(Max luma CB size): " << a.h265sps.log2_min_luma_coding_block_size_minus3 + 3 + a.h265sps.log2_diff_max_min_luma_coding_block_size << "\n";
                    oss << "Max ref pics: " << a.h265sps.max_num_ref_pics << "\n";
                    if (a.h265sps.hasCropping) {
                        oss << "Cropping: left=" << a.h265sps.crop_left
                            << " right=" << a.h265sps.crop_right
                            << " top=" << a.h265sps.crop_top
                            << " bottom=" << a.h265sps.crop_bottom << "\n";
                    }
                } else {
                    oss << "Failed to parse SPS\n";
                }
                break;
            }
            case 34: {
                a.isPPS = true;
                if (parseH265PPS(nal.data, a.h265pps)) {
                    oss << "=== H.265 PPS Details ===\n";
                    oss << "PPS ID: " << (int)a.h265pps.pps_id << "\n";
                    oss << "SPS ID: " << (int)a.h265pps.sps_id << "\n";
                    oss << "Init QP: " << 26 + a.h265pps.init_qp_minus26 << "\n";
                    oss << "Constrained intra pred: " << a.h265pps.constrained_intra_pred_flag << "\n";
                    oss << "Deblocking filter disabled: " << a.h265pps.pps_deblocking_filter_disabled_flag << "\n";
                } else {
                    oss << "Failed to parse PPS\n";
                }
                break;
            }
            case 19:
            case 20:
            case 21:
                oss << "=== H.265 Keyframe ===\nType: " << nal.typeName() << "\n";
                break;
            case 35:
                oss << "=== H.265 AUD (Access Unit Delimiter) ===\n";
                break;
            case 39:
            case 40:
                oss << "=== H.265 SEI ===\n";
                break;
            default:
                oss << "=== H.265 NAL Type " << (int)nal.nalType << " (" << nal.typeName() << ") ===\n";
                break;
        }
    }

    a.details = oss.str();
    return a;
}