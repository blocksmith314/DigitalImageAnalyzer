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
    int frameIndex = -1;   // 显示顺序序号（PTS，按 POC 重排后的顺序）
    int dtsIndex = -1;     // 解码顺序序号（DTS，图像在码流中出现的顺序）
    char frameType = 0;
    std::string typeName() const;
    std::string summary() const;
};