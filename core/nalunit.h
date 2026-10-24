#pragma once

#include <cstdint>
#include <vector>
#include <string>

enum class CodecType {
    H264,
    H265
};

struct NALUnit {
    CodecType codec = CodecType::H264;
    uint64_t offset = 0;
    uint32_t size = 0;
    uint8_t startCodeLen = 0;
    uint8_t headerByte = 0;
    uint8_t nalType = 0;
    uint8_t nuhLayerId = 0;
    uint16_t nuhTemporalIdPlus1 = 0;
    std::vector<uint8_t> data;
    int frameIndex = -1;   // presentation order index (PTS)
    int dtsIndex = -1;     // decode order index (DTS)
    char frameType = 0;
    std::string typeName() const;
    std::string summary() const;
};