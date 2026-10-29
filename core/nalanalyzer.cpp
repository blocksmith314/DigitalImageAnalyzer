#include "nalanalyzer.h"

#include <algorithm>
#include <sstream>

#include "bitstreamparser.h"
#include "seiparser.h"

bool NALAnalyzer::AnalyzeAll(BitstreamParser& parser,
                              const std::function<bool(double)>& progress) {
  stats_ = StreamStats();
  h264_sps_map_.clear();
  h264_pps_map_.clear();
  h265_sps_map_.clear();
  h265_pps_map_.clear();
  h265_vps_map_.clear();

  auto& nals = parser.NalUnitsMut();
  if (nals.empty()) return true;
  stats_.codec = nals.front().codec;
  stats_.total_nals = (uint32_t)nals.size();

  size_t processed = 0;
  for (auto& nal : nals) {
    if (progress && (++processed % 100000 == 0)) {
      if (!progress(0.8 * (double)processed / (double)nals.size())) {
        return false;
      }
    }
    const uint8_t* data = parser.NalData(nal);
    stats_.total_bytes += nal.size;
    if (nal.size > stats_.max_nal_size) stats_.max_nal_size = nal.size;
    if (stats_.min_nal_size == 0 || nal.size < stats_.min_nal_size)
      stats_.min_nal_size = nal.size;

    const bool slice_nal = (nal.codec == CodecType::kH264)
                               ? (nal.nal_type >= 1 && nal.nal_type <= 5)
                               : (nal.nal_type <= 21);
    if (slice_nal) stats_.slice_count++;

    if (nal.codec == CodecType::kH264) {
      switch (nal.nal_type) {
        case 7: {
          H264SPS sps;
          if (ParseH264Sps(nal, data, sps)) {
            h264_sps_map_[sps.sps_id] = sps;
            if (stats_.width == 0) {
              stats_.width = sps.width;
              stats_.height = sps.height;
              stats_.profile = sps.ProfileName();
              stats_.level = std::to_string(sps.level_idc);
            }
          }
          stats_.sps_count++;
          break;
        }
        case 8: {
          H264PPS pps;
          if (ParseH264Pps(nal, data, pps)) h264_pps_map_[pps.pps_id] = pps;
          stats_.pps_count++;
          break;
        }
        case 5:
          stats_.idr_count++;
          break;
        case 6:
          stats_.sei_count++;
          break;
        case 9:
          stats_.aud_count++;
          break;
        default:
          break;
      }
    } else {
      switch (nal.nal_type) {
        case 32: {
          H265VPS vps;
          if (ParseH265Vps(nal, data, vps)) h265_vps_map_[vps.vps_id] = vps;
          stats_.vps_count++;
          break;
        }
        case 33: {
          H265SPS sps;
          if (ParseH265Sps(nal, data, sps)) {
            h265_sps_map_[sps.sps_id] = sps;
            if (stats_.width == 0) {
              stats_.width = sps.width;
              stats_.height = sps.height;
              stats_.profile = sps.ProfileName();
              stats_.level = std::to_string(sps.level_idc);
            }
          }
          stats_.sps_count++;
          break;
        }
        case 34: {
          H265PPS pps;
          if (ParseH265Pps(nal, data, pps)) h265_pps_map_[pps.pps_id] = pps;
          stats_.pps_count++;
          break;
        }
        case 19:
        case 20:
        case 21:
          stats_.idr_count++;
          break;
        case 39:
        case 40:
          stats_.sei_count++;
          break;
        case 35:
          stats_.aud_count++;
          break;
        default:
          break;
      }
    }
  }

  const bool is_h264 = (stats_.codec == CodecType::kH264);

  struct FrameInfo {
    std::vector<NALUnit*> slices;
    uint32_t poc_lsb = 0;
    bool poc_valid = false;
    bool is_idr = false;
    char frame_type = 'P';
  };
  std::vector<FrameInfo> frames;
  FrameInfo* cur = nullptr;

  H264SPS activeSps;
  if (is_h264 && !h264_sps_map_.empty())
    activeSps = h264_sps_map_.begin()->second;

  processed = 0;
  for (auto& nal : nals) {
    if (progress && (++processed % 100000 == 0)) {
      if (!progress(0.8 + 0.2 * (double)processed / (double)nals.size())) {
        return false;
      }
    }
    const bool is_slice = is_h264 ? (nal.nal_type >= 1 && nal.nal_type <= 5)
                                  : (nal.nal_type <= 21);
    if (!is_slice) {
      nal.frame_index = -1;
      nal.dts_index = -1;
      nal.frame_type = 0;
      continue;
    }

    const uint8_t* data = parser.NalData(nal);
    bool is_new = !cur;
    uint32_t poc = 0;
    bool poc_ok = false;
    bool idr = false;
    char ft = 'P';

    if (is_h264) {
      H264SliceHeader sh;
      if (ParseH264SliceHeader(nal, data, activeSps, sh)) {
        if (sh.first_mb_in_slice == 0) is_new = true;
        poc = sh.pic_order_cnt_lsb;
        poc_ok = true;
        ft = sh.FrameTypeChar();
      }
      idr = (nal.nal_type == 5);
      if (idr) ft = 'I';
    } else {
      idr = (nal.nal_type >= 16 && nal.nal_type <= 23);
      if (idr) ft = 'I';

      uint32_t pps_id = 0;
      {
        RbspBitReader br(data + nal.start_code_len + 2,
                         nal.size - nal.start_code_len - 2);
        br.ReadBit();           // first_slice_segment_in_pic_flag
        if (idr) br.ReadBit();  // no_output_of_prior_pics_flag
        pps_id = (uint32_t)br.ReadUe();
      }
      auto pps_it = h265_pps_map_.find((int)pps_id);
      if (pps_it != h265_pps_map_.end()) {
        auto sps_it = h265_sps_map_.find((int)pps_it->second.sps_id);
        if (sps_it != h265_sps_map_.end()) {
          H265SliceSegmentHeader sh;
          if (ParseH265SliceSegmentHeader(nal, data, sps_it->second,
                                              pps_it->second, sh)) {
            if (sh.first_slice_segment_in_pic_flag) is_new = true;
            if (sh.poc_valid) {
              poc = sh.pic_order_cnt_lsb;
              poc_ok = true;
            }
            ft = sh.FrameTypeChar();
            idr = sh.is_irap;
          }
        }
      }
    }

    if (is_new) {
      frames.emplace_back();
      cur = &frames.back();
      cur->is_idr = idr;
    } else if (idr) {
      cur->is_idr = true;
    }
    cur->slices.push_back(&nal);
    if (poc_ok) {
      cur->poc_lsb = poc;
      cur->poc_valid = true;
    }
    cur->frame_type = ft;
  }

  if (!frames.empty()) {
    bool all_poc = is_h264 || (stats_.codec == CodecType::kH265);
    for (auto& f : frames) {
      if (!f.poc_valid) {
        all_poc = false;
        break;
      }
    }

    if (all_poc && frames.size() > 1) {
      uint32_t log2_poc = activeSps.log2_max_pic_order_cnt_lsb_minus4;
      if (!is_h264 && !h265_sps_map_.empty())
        log2_poc =
            h265_sps_map_.begin()->second.log2_max_pic_order_cnt_lsb_minus4;
      const uint32_t max_lsb = 1u << (log2_poc + 4);
      std::vector<int64_t> full_poc(frames.size(), 0);

      int next_idx = 0;
      int seg_start = 0;
      int32_t msb = 0;
      uint32_t prev_lsb = 0;
      for (int i = 1; i <= (int)frames.size(); i++) {
        const bool boundary = (i == (int)frames.size()) || frames[i].is_idr;
        if (!boundary) continue;

        msb = 0;
        prev_lsb = frames[seg_start].poc_lsb;
        full_poc[seg_start] = (int64_t)prev_lsb;
        for (int k = seg_start + 1; k < i; k++) {
          const uint32_t lsb = frames[k].poc_lsb;
          if (lsb < prev_lsb && (prev_lsb - lsb) >= max_lsb / 2) {
            msb += (int32_t)max_lsb;
          } else if (lsb > prev_lsb && (lsb - prev_lsb) > max_lsb / 2) {
            msb -= (int32_t)max_lsb;
          }
          full_poc[k] = (int64_t)msb + lsb;
          prev_lsb = lsb;
        }

        std::vector<int> idxs;
        for (int k = seg_start; k < i; k++) idxs.push_back(k);
        std::stable_sort(idxs.begin(), idxs.end(), [&](int a, int b) {
          return full_poc[a] < full_poc[b];
        });
        for (int k = 0; k < (int)idxs.size(); k++) {
          for (auto* s : frames[idxs[k]].slices) {
            s->frame_index = next_idx++;
            s->dts_index = idxs[k];
            s->frame_type = frames[idxs[k]].frame_type;
          }
        }
        seg_start = i;
      }
    } else {
      for (int i = 0; i < (int)frames.size(); i++) {
        for (auto* s : frames[i].slices) {
          s->frame_index = i;
          s->dts_index = i;
          s->frame_type = frames[i].frame_type;
        }
      }
    }
  }

  stats_.FrameCount = (uint32_t)frames.size();
  stats_.i_frame_count = stats_.p_frame_count = stats_.b_frame_count = 0;
  for (auto& f : frames) {
    switch (f.frame_type) {
      case 'I':
        stats_.i_frame_count++;
        break;
      case 'P':
        stats_.p_frame_count++;
        break;
      case 'B':
        stats_.b_frame_count++;
        break;
      default:
        break;
    }
  }
  if (progress) progress(1.0);
  return true;
}

NALAnalysis NALAnalyzer::AnalyzeOne(const NALUnit& nal,
                                     const uint8_t* data) const {
  NALAnalysis a;
  std::ostringstream oss;

  if (nal.codec == CodecType::kH264) {
    switch (nal.nal_type) {
      case 7: {
        a.is_sps = true;
        if (ParseH264Sps(nal, data, a.h264sps)) {
          oss << "=== H.264 SPS Details ===\n";
          oss << "Profile: " << a.h264sps.ProfileName() << " ("
              << (int)a.h264sps.profile_idc << ")\n";
          oss << "Level: " << (int)a.h264sps.level_idc << "\n";
          oss << "SPS ID: " << (int)a.h264sps.sps_id << "\n";
          oss << "Resolution: " << a.h264sps.width << " x " << a.h264sps.height
              << "\n";
          oss << "Chroma format: " << a.h264sps.chroma_format_idc << "\n";
          oss << "Bit depth: luma=" << 8 + a.h264sps.bit_depth_luma_minus8
              << " chroma=" << 8 + a.h264sps.bit_depth_chroma_minus8 << "\n";
          oss << "Max frame num: "
              << (1u << (a.h264sps.log2_max_frame_num_minus4 + 4)) << "\n";
          oss << "PIC order cnt type: " << a.h264sps.pic_order_cnt_type << "\n";
          if (a.h264sps.pic_order_cnt_type == 0)
            oss << "Log2 max POC LSB: "
                << a.h264sps.log2_max_pic_order_cnt_lsb_minus4 + 4 << "\n";
          oss << "Max ref frames: " << a.h264sps.max_num_ref_frames << "\n";
          oss << "Frame only: " << a.h264sps.frame_mbs_only_flag << "\n";
          oss << "Direct 8x8: " << a.h264sps.direct_8x8_inference_flag << "\n";
          oss << "Constraint flags: " << a.h264sps.constraint_set0_flag
              << a.h264sps.constraint_set1_flag
              << a.h264sps.constraint_set2_flag
              << a.h264sps.constraint_set3_flag
              << a.h264sps.constraint_set4_flag
              << a.h264sps.constraint_set5_flag << "\n";
          if (a.h264sps.has_cropping) {
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
                  << (double)a.h264sps.time_scale /
                         (2.0 * a.h264sps.num_units_in_tick)
                  << " fps\n";
            }
          }
          if (a.h264sps.bitstream_restriction_flag) {
            oss << "Max reorder frames: " << a.h264sps.max_num_reorder_frames
                << "\n";
            oss << "Max dec frame buffering: "
                << a.h264sps.max_dec_frame_buffering << "\n";
          }
        } else {
          oss << "Failed to parse SPS\n";
        }
        break;
      }
      case 8: {
        a.is_pps = true;
        if (ParseH264Pps(nal, data, a.h264pps)) {
          oss << "=== H.264 PPS Details ===\n";
          oss << "PPS ID: " << (int)a.h264pps.pps_id << "\n";
          oss << "SPS ID: " << (int)a.h264pps.sps_id << "\n";
          oss << "Entropy coding (CABAC): "
              << a.h264pps.entropy_coding_mode_flag << "\n";
          oss << "Slice groups: " << a.h264pps.num_slice_groups_minus1 + 1
              << "\n";
          if (a.h264pps.num_slice_groups_minus1 > 0)
            oss << "Slice group map type: " << a.h264pps.slice_group_map_type
                << "\n";
          oss << "Ref idx L0/L1: "
              << a.h264pps.num_ref_idx_l0_default_active_minus1 + 1 << "/"
              << a.h264pps.num_ref_idx_l1_default_active_minus1 + 1 << "\n";
          oss << "Weighted pred: " << a.h264pps.weighted_pred_flag << "\n";
          oss << "Weighted bipred idc: " << (int)a.h264pps.weighted_bipred_idc
              << "\n";
          oss << "Init QP: " << 26 + a.h264pps.pic_init_qp_minus26 << "\n";
          oss << "Chroma QP offset: " << a.h264pps.chroma_qp_index_offset
              << "\n";
          oss << "Deblocking filter present: "
              << a.h264pps.deblocking_filter_control_present_flag << "\n";
          oss << "Constrained intra pred: "
              << a.h264pps.constrained_intra_pred_flag << "\n";
          oss << "Redundant PIC cnt: "
              << a.h264pps.redundant_pic_cnt_present_flag << "\n";
          oss << "Transform 8x8: " << a.h264pps.transform_8x8_mode_flag << "\n";
          oss << "Second chroma QP offset: "
              << a.h264pps.second_chroma_qp_index_offset << "\n";
        } else {
          oss << "Failed to parse PPS\n";
        }
        break;
      }
      case 5:
        oss << "=== H.264 IDR Slice ===\nType: Instantaneous Decoding Refresh "
               "(keyframe)\n";
        break;
      case 1:
        oss << "=== H.264 Non-IDR Slice ===\n";
        break;
      case 6: {
        oss << "=== H.264 SEI (Supplemental Enhancement Information) ===\n";
        std::vector<SeiMessage> msgs;
        if (ParseSeiMessages(nal, data, false, msgs) && !msgs.empty()) {
          for (size_t i = 0; i < msgs.size(); i++) {
            oss << "Message #" << i << ": type " << msgs[i].type << " ("
                << SeiTypeName(msgs[i].type) << "), " << msgs[i].size
                << " bytes\n";
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
        oss << "=== H.264 NAL Type " << (int)nal.nal_type << " ===\n";
        break;
    }
  } else {
    switch (nal.nal_type) {
      case 32: {
        a.is_vps = true;
        if (ParseH265Vps(nal, data, a.h265vps)) {
          oss << "=== H.265 VPS Details ===\n";
          oss << "VPS ID: " << (int)a.h265vps.vps_id << "\n";
          oss << "Base layer internal/available: "
              << a.h265vps.base_layer_internal_flag << "/"
              << a.h265vps.base_layer_available_flag << "\n";
          oss << "Max layers: " << (int)a.h265vps.max_layers_minus1 + 1 << "\n";
          oss << "Max sub-layers: " << (int)a.h265vps.max_sub_layers_minus1 + 1
              << "\n";
          oss << "Temporal ID nesting: " << a.h265vps.temporal_id_nesting_flag
              << "\n";
          oss << "Max layer ID: " << (int)a.h265vps.max_layer_id << "\n";
        } else {
          oss << "Failed to parse VPS\n";
        }
        break;
      }
      case 33: {
        a.is_sps = true;
        if (ParseH265Sps(nal, data, a.h265sps)) {
          oss << "=== H.265 SPS Details ===\n";
          oss << "VPS ID: " << (int)a.h265sps.vps_id << "\n";
          oss << "SPS ID: " << (int)a.h265sps.sps_id << "\n";
          oss << "Profile: " << a.h265sps.ProfileName() << " ("
              << (int)a.h265sps.profile_idc << ")  tier=" << a.h265sps.tier_flag
              << "  space=" << (int)a.h265sps.profile_space << "\n";
          oss << "Level: " << (int)a.h265sps.level_idc << "\n";
          oss << "Resolution: " << a.h265sps.width << " x " << a.h265sps.height
              << "\n";
          oss << "Pic size (luma): " << a.h265sps.pic_width_in_luma_samples
              << " x " << a.h265sps.pic_height_in_luma_samples << "\n";
          oss << "Chroma format: " << (int)a.h265sps.chroma_format_idc << "\n";
          oss << "Bit depth: luma=" << 8 + a.h265sps.bit_depth_luma_minus8
              << " chroma=" << 8 + a.h265sps.bit_depth_chroma_minus8 << "\n";
          oss << "CTU size: "
              << (1u << (a.h265sps.log2_min_luma_coding_block_size_minus3 + 3 +
                         a.h265sps.log2_diff_max_min_luma_coding_block_size))
              << "\n";
          oss << "Min CB size: "
              << (1u << (a.h265sps.log2_min_luma_coding_block_size_minus3 + 3))
              << "\n";
          oss << "Transform depth (inter/intra): "
              << a.h265sps.max_transform_hierarchy_depth_inter << "/"
              << a.h265sps.max_transform_hierarchy_depth_intra << "\n";
          oss << "Max dec pic buffering: "
              << a.h265sps.max_dec_pic_buffering_minus1 + 1 << "\n";
          oss << "Max reorder pics: " << a.h265sps.max_num_reorder_pics << "\n";
          oss << "Log2 max POC LSB: "
              << a.h265sps.log2_max_pic_order_cnt_lsb_minus4 + 4 << "\n";
          oss << "Short-term RPS: " << a.h265sps.num_short_term_ref_pic_sets
              << "\n";
          oss << "AMP/SAO: " << a.h265sps.amp_enabled_flag << "/"
              << a.h265sps.sample_adaptive_offset_enabled_flag << "\n";
          if (a.h265sps.vui_timing_info_present_flag &&
              a.h265sps.vui_num_units_in_tick > 0) {
            oss << "Frame rate: "
                << (double)a.h265sps.vui_time_scale /
                       a.h265sps.vui_num_units_in_tick
                << " fps\n";
          }
          if (a.h265sps.has_cropping) {
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
        a.is_pps = true;
        if (ParseH265Pps(nal, data, a.h265pps)) {
          oss << "=== H.265 PPS Details ===\n";
          oss << "PPS ID: " << (int)a.h265pps.pps_id << "\n";
          oss << "SPS ID: " << (int)a.h265pps.sps_id << "\n";
          oss << "Dependent slice segments: "
              << a.h265pps.dependent_slice_segments_enabled_flag << "\n";
          oss << "Output flag present: " << a.h265pps.output_flag_present_flag
              << "\n";
          oss << "Extra slice header bits: "
              << (int)a.h265pps.num_extra_slice_header_bits << "\n";
          oss << "Sign data hiding: " << a.h265pps.sign_data_hiding_enabled_flag
              << "\n";
          oss << "CABAC init present: " << a.h265pps.cabac_init_present_flag
              << "\n";
          oss << "Ref idx L0/L1: "
              << a.h265pps.num_ref_idx_l0_default_active_minus1 + 1 << "/"
              << a.h265pps.num_ref_idx_l1_default_active_minus1 + 1 << "\n";
          oss << "Init QP: " << 26 + a.h265pps.init_qp_minus26 << "\n";
          oss << "Constrained intra pred: "
              << a.h265pps.constrained_intra_pred_flag << "\n";
          oss << "Transform skip: " << a.h265pps.transform_skip_enabled_flag
              << "\n";
          oss << "CU QP delta: " << a.h265pps.cu_qp_delta_enabled_flag;
          if (a.h265pps.cu_qp_delta_enabled_flag)
            oss << " (depth " << a.h265pps.diff_cu_qp_delta_depth << ")";
          oss << "\n";
          oss << "Chroma QP offset (Cb/Cr): " << a.h265pps.pps_cb_qp_offset
              << "/" << a.h265pps.pps_cr_qp_offset << "\n";
          oss << "Weighted pred/bipred: " << a.h265pps.weighted_pred_flag << "/"
              << a.h265pps.weighted_bipred_flag << "\n";
          oss << "Tiles: " << a.h265pps.tiles_enabled_flag;
          if (a.h265pps.tiles_enabled_flag)
            oss << " (" << a.h265pps.num_tile_columns_minus1 + 1 << " x "
                << a.h265pps.num_tile_rows_minus1 + 1 << ")";
          oss << "\n";
          oss << "Entropy coding sync: "
              << a.h265pps.entropy_coding_sync_enabled_flag << "\n";
          oss << "Deblocking filter control present: "
              << a.h265pps.deblocking_filter_control_present_flag << "\n";
          oss << "Deblocking filter disabled: "
              << a.h265pps.pps_deblocking_filter_disabled_flag << "\n";
        } else {
          oss << "Failed to parse PPS\n";
        }
        break;
      }
      case 19:
      case 20:
      case 21:
        oss << "=== H.265 Keyframe ===\nType: " << nal.TypeName() << "\n";
        break;
      case 35:
        oss << "=== H.265 AUD (Access Unit Delimiter) ===\n";
        break;
      case 39:
      case 40: {
        oss << "=== H.265 " << (nal.nal_type == 39 ? "Prefix" : "Suffix")
            << " SEI ===\n";
        std::vector<SeiMessage> msgs;
        if (ParseSeiMessages(nal, data, true, msgs) && !msgs.empty()) {
          for (size_t i = 0; i < msgs.size(); i++) {
            oss << "Message #" << i << ": type " << msgs[i].type << " ("
                << SeiTypeName(msgs[i].type) << "), " << msgs[i].size
                << " bytes\n";
            oss << "    " << msgs[i].text << "\n";
          }
        } else {
          oss << "No SEI messages found\n";
        }
        break;
      }
      default:
        oss << "=== H.265 NAL Type " << (int)nal.nal_type << " ("
            << nal.TypeName() << ") ===\n";
        break;
    }
  }

  a.details = oss.str();
  return a;
}
