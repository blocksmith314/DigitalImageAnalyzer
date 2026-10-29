#ifndef DIA_CORE_SEIPARSER_H_
#define DIA_CORE_SEIPARSER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "nalunit.h"

struct SeiMessage {
  uint32_t type = 0;
  uint32_t size = 0;
  std::string text;  // decoded description
};

// Parse the SEI messages carried by a single SEI NAL unit (H.264 type 6,
// H.265 prefix/suffix SEI). Returns true if at least one message was parsed.
bool ParseSeiMessages(const NALUnit& nal, const uint8_t* data, bool h265,
                        std::vector<SeiMessage>& out);

// Human readable name for the SEI payload type.
std::string SeiTypeName(uint32_t type);

#endif  // DIA_CORE_SEIPARSER_H_
