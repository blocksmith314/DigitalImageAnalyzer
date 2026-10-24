#pragma once

#include "nalunit.h"
#include <cstdint>
#include <string>
#include <vector>

struct SeiMessage {
    uint32_t type = 0;
    uint32_t size = 0;
    std::string text;   // decoded description
};

// Parse the SEI messages carried by a single SEI NAL unit (H.264 type 6,
// H.265 prefix/suffix SEI). Returns true if at least one message was parsed.
bool parseSeiMessages(const NALUnit &nal, bool h265, std::vector<SeiMessage> &out);

// Human readable name for the SEI payload type.
std::string seiTypeName(uint32_t type);
