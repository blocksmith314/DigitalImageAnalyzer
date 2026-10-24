#include "nalanalyzer.h"
#include "seiparser.h"
#include <sstream>
#include <algorithm>

void NALAnalyzer::analyzeAll(std::vector<NALUnit> &nals) {
    m_stats = StreamStats();
    m_h264SpsMap.clear();
    m_h264PpsMap.clear();
    m_h265SpsMap.clear();
    m_h265PpsMap.clear();
    m_h265VpsMap.clear();

    if (nals.empty()) return;
    m_stats.codec = nals.front().codec;
    m_stats.totalNals = (uint32_t)nals.size();

    for (auto &nal : nals) {
        m_stats.totalBytes += nal.size;
        if (nal.size > m_stats.maxNalSize) m_stats.maxNalSize = nal.size;
        if (m_stats.minNalSize == 0 || nal.size < m_stats.minNalSize)
            m_stats.minNalSize = nal.size;
        const bool sliceNal = (nal.codec == CodecType::H264)
                                  ? (nal.nalType >= 1 && nal.nalType <= 5)
                                  : (nal.nalType <= 21);
        if (sliceNal) m_stats.sliceCount++;
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
                    m_stats.level = std::to_string(a.h265sps.level_idc);
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
        } else {
            if (nal.nalType == 19 || nal.nalType == 20 || nal.nalType == 21) m_stats.idrCount++;
            if (nal.nalType == 39 || nal.nalType == 40) m_stats.seiCount++;
            if (nal.nalType == 35) m_stats.audCount++;
        }
    }

    bool isH264 = (m_stats.codec == CodecType::H264);

    struct FrameInfo {
        std::vector<NALUnit*> slices;
        uint32_t poc_lsb = 0;
        bool pocValid = false;
        bool isIdr = false;
        char frameType = 'P';
    };
    std::vector<FrameInfo> frames;
    FrameInfo *cur = nullptr;

    H264SPS activeSps;
    if (isH264 && !m_h264SpsMap.empty()) activeSps = m_h264SpsMap.begin()->second;

    for (auto &nal : nals) {
        bool isSlice = isH264 ? (nal.nalType >= 1 && nal.nalType <= 5) : (nal.nalType <= 21);
        if (!isSlice) {
            nal.frameIndex = -1;
            nal.dtsIndex = -1;
            nal.frameType = 0;
            continue;
        }

        bool isNew = !cur;
        uint32_t poc = 0;
        bool pocOk = false;
        bool idr = false;
        char ft = 'P';

        if (isH264) {
            H264SliceHeader sh;
            if (parseH264SliceHeader(nal, activeSps, sh)) {
                bool mb0 = (sh.first_mb_in_slice == 0);
                if (mb0) isNew = true;
                poc = sh.pic_order_cnt_lsb;
                pocOk = true;
                ft = sh.frameTypeChar();
            }
            idr = (nal.nalType == 5);
            if (idr) ft = 'I';
        } else {
            // H.265: IRAP (BLA/IDR/CRA) resets POC; use it as a segment boundary
            idr = (nal.nalType >= 16 && nal.nalType <= 21);
            if (idr) ft = 'I';
        }

        if (isNew) {
            frames.emplace_back();
            cur = &frames.back();
            cur->isIdr = idr;
        } else if (idr) {
            cur->isIdr = true;
        }
        cur->slices.push_back(&nal);
        if (pocOk) { cur->poc_lsb = poc; cur->pocValid = true; }
        cur->frameType = ft;
    }

    if (!frames.empty()) {
        bool allPoc = isH264;
        for (auto &f : frames) {
            if (!f.pocValid) { allPoc = false; break; }
        }

        if (allPoc && frames.size() > 1) {
            // Display order: POC LSB is only valid within one coded video
            // sequence (between IDRs), so sort each IDR segment by POC and
            // accumulate the frame index across segments.
            const uint32_t maxLsb =
                1u << (activeSps.log2_max_pic_order_cnt_lsb_minus4 + 4);
            std::vector<int64_t> fullPoc(frames.size(), 0);

            int nextIdx = 0;
            int segStart = 0;
            int32_t msb = 0;
            uint32_t prevLsb = 0;
            for (int i = 1; i <= (int)frames.size(); i++) {
                bool boundary = (i == (int)frames.size()) || frames[i].isIdr;
                if (!boundary) continue;

                // Segment [segStart, i): unwrap POC LSB in decode order
                msb = 0;
                prevLsb = frames[segStart].poc_lsb;
                fullPoc[segStart] = (int64_t)prevLsb;
                for (int k = segStart + 1; k < i; k++) {
                    uint32_t lsb = frames[k].poc_lsb;
                    if (lsb < prevLsb && (prevLsb - lsb) >= maxLsb / 2) {
                        msb += (int32_t)maxLsb;
                    } else if (lsb > prevLsb && (lsb - prevLsb) > maxLsb / 2) {
                        msb -= (int32_t)maxLsb;
                    }
                    fullPoc[k] = (int64_t)msb + lsb;
                    prevLsb = lsb;
                }

                std::vector<int> idxs;
                for (int k = segStart; k < i; k++) idxs.push_back(k);
                std::stable_sort(idxs.begin(), idxs.end(), [&](int a, int b) {
                    return fullPoc[a] < fullPoc[b];
                });
                for (int k = 0; k < (int)idxs.size(); k++) {
                    for (auto *s : frames[idxs[k]].slices) {
                        s->frameIndex = nextIdx++;
                        s->dtsIndex = idxs[k];
                        s->frameType = frames[idxs[k]].frameType;
                    }
                }
                segStart = i;
            }
        } else {
            for (int i = 0; i < (int)frames.size(); i++) {
                for (auto *s : frames[i].slices) {
                    s->frameIndex = i;
                    s->dtsIndex = i;
                    s->frameType = frames[i].frameType;
                }
            }
        }
    }

    m_stats.frameCount = (uint32_t)frames.size();
    m_stats.iFrameCount = m_stats.pFrameCount = m_stats.bFrameCount = 0;
    for (auto &f : frames) {
        switch (f.frameType) {
            case 'I': m_stats.iFrameCount++; break;
            case 'P': m_stats.pFrameCount++; break;
            case 'B': m_stats.bFrameCount++; break;
            default: break;
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
                if (parseH264SPS(nal, a.h264sps)) {
                    oss << "=== H.264 SPS Details ===\n";
                    oss << "Profile: " << a.h264sps.profileName() << " (" << (int)a.h264sps.profile_idc << ")\n";
                    oss << "Level: " << (int)a.h264sps.level_idc << "\n";
                    oss << "SPS ID: " << (int)a.h264sps.sps_id << "\n";
                    oss << "Resolution: " << a.h264sps.width << " x " << a.h264sps.height << "\n";
                    oss << "Chroma format: " << a.h264sps.chroma_format_idc << "\n";
                    oss << "Bit depth: luma=" << 8 + a.h264sps.bit_depth_luma_minus8
                        << " chroma=" << 8 + a.h264sps.bit_depth_chroma_minus8 << "\n";
                    oss << "Max frame num: " << (1u << (a.h264sps.log2_max_frame_num_minus4 + 4)) << "\n";
                    oss << "PIC order cnt type: " << a.h264sps.pic_order_cnt_type << "\n";
                    if (a.h264sps.pic_order_cnt_type == 0)
                        oss << "Log2 max POC LSB: " << a.h264sps.log2_max_pic_order_cnt_lsb_minus4 + 4 << "\n";
                    oss << "Max ref frames: " << a.h264sps.max_num_ref_frames << "\n";
                    oss << "Frame only: " << a.h264sps.frame_mbs_only_flag << "\n";
                    oss << "Direct 8x8: " << a.h264sps.direct_8x8_inference_flag << "\n";
                    oss << "Constraint flags: " << a.h264sps.constraint_set0_flag << a.h264sps.constraint_set1_flag
                        << a.h264sps.constraint_set2_flag << a.h264sps.constraint_set3_flag
                        << a.h264sps.constraint_set4_flag << a.h264sps.constraint_set5_flag << "\n";
                    if (a.h264sps.hasCropping) {
                        oss << "Cropping: left=" << a.h264sps.crop_left
                            << " right=" << a.h264sps.crop_right
                            << " top=" << a.h264sps.crop_top
                            << " bottom=" << a.h264sps.crop_bottom << "\n";
                    }
                    if (a.h264sps.timing_info_present_flag) {
                        oss << "Timing: num_units_in_tick=" << a.h264sps.num_units_in_tick
                            << " time_scale=" << a.h264sps.time_scale << "\n";
                        if (a.h264sps.num_units_in_tick > 0) {
                            oss << "Frame rate: "
                                << (double)a.h264sps.time_scale / (2.0 * a.h264sps.num_units_in_tick)
                                << " fps\n";
                        }
                    }
                    if (a.h264sps.bitstream_restriction_flag) {
                        oss << "Max reorder frames: " << a.h264sps.max_num_reorder_frames << "\n";
                        oss << "Max dec frame buffering: " << a.h264sps.max_dec_frame_buffering << "\n";
                    }
                } else {
                    oss << "Failed to parse SPS\n";
                }
                break;
            }
            case 8: {
                a.isPPS = true;
                if (parseH264PPS(nal, a.h264pps)) {
                    oss << "=== H.264 PPS Details ===\n";
                    oss << "PPS ID: " << (int)a.h264pps.pps_id << "\n";
                    oss << "SPS ID: " << (int)a.h264pps.sps_id << "\n";
                    oss << "Entropy coding (CABAC): " << a.h264pps.entropy_coding_mode_flag << "\n";
                    oss << "Slice groups: " << a.h264pps.num_slice_groups_minus1 + 1 << "\n";
                    if (a.h264pps.num_slice_groups_minus1 > 0)
                        oss << "Slice group map type: " << a.h264pps.slice_group_map_type << "\n";
                    oss << "Ref idx L0/L1: " << a.h264pps.num_ref_idx_l0_default_active_minus1 + 1
                        << "/" << a.h264pps.num_ref_idx_l1_default_active_minus1 + 1 << "\n";
                    oss << "Weighted pred: " << a.h264pps.weighted_pred_flag << "\n";
                    oss << "Weighted bipred idc: " << (int)a.h264pps.weighted_bipred_idc << "\n";
                    oss << "Init QP: " << 26 + a.h264pps.pic_init_qp_minus26 << "\n";
                    oss << "Chroma QP offset: " << a.h264pps.chroma_qp_index_offset << "\n";
                    oss << "Deblocking filter present: " << a.h264pps.deblocking_filter_control_present_flag << "\n";
                    oss << "Constrained intra pred: " << a.h264pps.constrained_intra_pred_flag << "\n";
                    oss << "Redundant PIC cnt: " << a.h264pps.redundant_pic_cnt_present_flag << "\n";
                    oss << "Transform 8x8: " << a.h264pps.transform_8x8_mode_flag << "\n";
                    oss << "Second chroma QP offset: " << a.h264pps.second_chroma_qp_index_offset << "\n";
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
            case 6: {
                oss << "=== H.264 SEI (Supplemental Enhancement Information) ===\n";
                std::vector<SeiMessage> msgs;
                if (parseSeiMessages(nal, false, msgs) && !msgs.empty()) {
                    for (size_t i = 0; i < msgs.size(); i++) {
                        oss << "Message #" << i << ": type " << msgs[i].type
                            << " (" << seiTypeName(msgs[i].type) << "), "
                            << msgs[i].size << " bytes\n";
                        oss << "    " << msgs[i].text << "\n";
                    }
                } else {
                    oss << "No SEI messages found\n";
                }
                break;
            }
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
                if (parseH265VPS(nal, a.h265vps)) {
                    oss << "=== H.265 VPS Details ===\n";
                    oss << "VPS ID: " << (int)a.h265vps.vps_id << "\n";
                    oss << "Base layer internal/available: " << a.h265vps.base_layer_internal_flag
                        << "/" << a.h265vps.base_layer_available_flag << "\n";
                    oss << "Max layers: " << (int)a.h265vps.max_layers_minus1 + 1 << "\n";
                    oss << "Max sub-layers: " << (int)a.h265vps.max_sub_layers_minus1 + 1 << "\n";
                    oss << "Temporal ID nesting: " << a.h265vps.temporal_id_nesting_flag << "\n";
                    oss << "Max layer ID: " << (int)a.h265vps.max_layer_id << "\n";
                } else {
                    oss << "Failed to parse VPS\n";
                }
                break;
            }
            case 33: {
                a.isSPS = true;
                if (parseH265SPS(nal, a.h265sps)) {
                    oss << "=== H.265 SPS Details ===\n";
                    oss << "VPS ID: " << (int)a.h265sps.vps_id << "\n";
                    oss << "SPS ID: " << (int)a.h265sps.sps_id << "\n";
                    oss << "Profile: " << a.h265sps.profileName() << " (" << (int)a.h265sps.profile_idc
                        << ")  tier=" << a.h265sps.tier_flag << "  space=" << (int)a.h265sps.profile_space << "\n";
                    oss << "Level: " << (int)a.h265sps.level_idc << "\n";
                    oss << "Resolution: " << a.h265sps.width << " x " << a.h265sps.height << "\n";
                    oss << "Pic size (luma): " << a.h265sps.pic_width_in_luma_samples
                        << " x " << a.h265sps.pic_height_in_luma_samples << "\n";
                    oss << "Chroma format: " << (int)a.h265sps.chroma_format_idc << "\n";
                    oss << "Bit depth: luma=" << 8 + a.h265sps.bit_depth_luma_minus8
                        << " chroma=" << 8 + a.h265sps.bit_depth_chroma_minus8 << "\n";
                    oss << "CTU size: " << (1u << (a.h265sps.log2_min_luma_coding_block_size_minus3 + 3 + a.h265sps.log2_diff_max_min_luma_coding_block_size)) << "\n";
                    oss << "Min CB size: " << (1u << (a.h265sps.log2_min_luma_coding_block_size_minus3 + 3)) << "\n";
                    oss << "Transform depth (inter/intra): " << a.h265sps.max_transform_hierarchy_depth_inter
                        << "/" << a.h265sps.max_transform_hierarchy_depth_intra << "\n";
                    oss << "Max dec pic buffering: " << a.h265sps.max_dec_pic_buffering_minus1 + 1 << "\n";
                    oss << "Max reorder pics: " << a.h265sps.max_num_reorder_pics << "\n";
                    oss << "Log2 max POC LSB: " << a.h265sps.log2_max_pic_order_cnt_lsb_minus4 + 4 << "\n";
                    oss << "Short-term RPS: " << a.h265sps.num_short_term_ref_pic_sets << "\n";
                    oss << "AMP/SAO: " << a.h265sps.amp_enabled_flag << "/" << a.h265sps.sample_adaptive_offset_enabled_flag << "\n";
                    if (a.h265sps.vui_timing_info_present_flag && a.h265sps.vui_num_units_in_tick > 0) {
                        oss << "Frame rate: "
                            << (double)a.h265sps.vui_time_scale / a.h265sps.vui_num_units_in_tick << " fps\n";
                    }
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
                if (parseH265PPS(nal, a.h265pps)) {
                    oss << "=== H.265 PPS Details ===\n";
                    oss << "PPS ID: " << (int)a.h265pps.pps_id << "\n";
                    oss << "SPS ID: " << (int)a.h265pps.sps_id << "\n";
                    oss << "Dependent slice segments: " << a.h265pps.dependent_slice_segments_enabled_flag << "\n";
                    oss << "Output flag present: " << a.h265pps.output_flag_present_flag << "\n";
                    oss << "Extra slice header bits: " << (int)a.h265pps.num_extra_slice_header_bits << "\n";
                    oss << "Sign data hiding: " << a.h265pps.sign_data_hiding_enabled_flag << "\n";
                    oss << "CABAC init present: " << a.h265pps.cabac_init_present_flag << "\n";
                    oss << "Ref idx L0/L1: " << a.h265pps.num_ref_idx_l0_default_active_minus1 + 1
                        << "/" << a.h265pps.num_ref_idx_l1_default_active_minus1 + 1 << "\n";
                    oss << "Init QP: " << 26 + a.h265pps.init_qp_minus26 << "\n";
                    oss << "Constrained intra pred: " << a.h265pps.constrained_intra_pred_flag << "\n";
                    oss << "Transform skip: " << a.h265pps.transform_skip_enabled_flag << "\n";
                    oss << "CU QP delta: " << a.h265pps.cu_qp_delta_enabled_flag;
                    if (a.h265pps.cu_qp_delta_enabled_flag)
                        oss << " (depth " << a.h265pps.diff_cu_qp_delta_depth << ")";
                    oss << "\n";
                    oss << "Chroma QP offset (Cb/Cr): " << a.h265pps.pps_cb_qp_offset
                        << "/" << a.h265pps.pps_cr_qp_offset << "\n";
                    oss << "Weighted pred/bipred: " << a.h265pps.weighted_pred_flag
                        << "/" << a.h265pps.weighted_bipred_flag << "\n";
                    oss << "Tiles: " << a.h265pps.tiles_enabled_flag;
                    if (a.h265pps.tiles_enabled_flag)
                        oss << " (" << a.h265pps.num_tile_columns_minus1 + 1
                            << " x " << a.h265pps.num_tile_rows_minus1 + 1 << ")";
                    oss << "\n";
                    oss << "Entropy coding sync: " << a.h265pps.entropy_coding_sync_enabled_flag << "\n";
                    oss << "Deblocking filter control present: " << a.h265pps.deblocking_filter_control_present_flag << "\n";
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
            case 40: {
                oss << "=== H.265 " << (nal.nalType == 39 ? "Prefix" : "Suffix") << " SEI ===\n";
                std::vector<SeiMessage> msgs;
                if (parseSeiMessages(nal, true, msgs) && !msgs.empty()) {
                    for (size_t i = 0; i < msgs.size(); i++) {
                        oss << "Message #" << i << ": type " << msgs[i].type
                            << " (" << seiTypeName(msgs[i].type) << "), "
                            << msgs[i].size << " bytes\n";
                        oss << "    " << msgs[i].text << "\n";
                    }
                } else {
                    oss << "No SEI messages found\n";
                }
                break;
            }
            default:
                oss << "=== H.265 NAL Type " << (int)nal.nalType << " (" << nal.typeName() << ") ===\n";
                break;
        }
    }

    a.details = oss.str();
    return a;
}