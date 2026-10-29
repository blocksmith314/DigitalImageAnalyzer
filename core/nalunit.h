#ifndef DIA_CORE_NALUNIT_H_
#define DIA_CORE_NALUNIT_H_

#include <cstdint>
#include <string>
#include <vector>

enum class CodecType { kH264, kH265 };

// Metadata for one NAL unit. The payload bytes are not stored here; use
// BitstreamParser::NalData() to access them on demand (keeps multi-GB files
// out of memory).
struct NALUnit {
  CodecType codec = CodecType::kH264;
  uint64_t offset = 0;  // byte offset of the start code
  uint32_t size = 0;    // bytes, including the start code
  uint8_t start_code_len = 0;
  uint8_t header_byte = 0;
  uint8_t nal_type = 0;
  uint8_t nuh_layer_id = 0;
  uint16_t nuh_temporal_id_plus1 = 0;
  int frame_index = -1;  // presentation order index (PTS)
  int dts_index = -1;    // decode order index (DTS)
  char frame_type = 0;
  std::string TypeName() const;
  std::string summary() const;
};

#endif  // DIA_CORE_NALUNIT_H_
