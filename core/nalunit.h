#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class CodecType
{
    H264,
    H265
};

// Metadata for one NAL unit. The payload bytes are not stored here; use
// BitstreamParser::nalData() to access them on demand (keeps multi-GB files
// out of memory).
struct NALUnit
{
    CodecType codec = CodecType::H264;
    uint64_t offset = 0; // byte offset of the start code
    uint32_t size = 0; // bytes, including the start code
    uint8_t startCodeLen = 0;
    uint8_t headerByte = 0;
    uint8_t nalType = 0;
    uint8_t nuhLayerId = 0;
    uint16_t nuhTemporalIdPlus1 = 0;
    int frameIndex = -1; // presentation order index (PTS)
    int dtsIndex = -1; // decode order index (DTS)
    char frameType = 0;
    std::string typeName() const;
    std::string summary() const;
};
