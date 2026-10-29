#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "h264types.h"
#include "h265types.h"
#include "nalunit.h"

struct NALAnalysis
{
    bool isSPS = false;
    bool isPPS = false;
    bool isVPS = false;
    H264SPS h264sps;
    H264PPS h264pps;
    H265SPS h265sps;
    H265PPS h265pps;
    H265VPS h265vps;
    std::string details;
};

struct StreamStats
{
    CodecType codec = CodecType::H264;
    uint64_t totalBytes = 0;
    uint32_t totalNals = 0;
    uint32_t idrCount = 0;
    uint32_t spsCount = 0;
    uint32_t ppsCount = 0;
    uint32_t vpsCount = 0;
    uint32_t seiCount = 0;
    uint32_t audCount = 0;
    uint32_t frameCount = 0;
    uint32_t sliceCount = 0; // number of slice NALs
    uint32_t iFrameCount = 0;
    uint32_t pFrameCount = 0;
    uint32_t bFrameCount = 0;
    uint32_t maxNalSize = 0;
    uint32_t minNalSize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string profile;
    std::string level;
};

class BitstreamParser;

class NALAnalyzer
{
public:
    bool analyzeAll(BitstreamParser& parser, const std::function<bool(double)>& progress = {});
    NALAnalysis analyzeOne(const NALUnit& nal, const uint8_t* data) const;
    const StreamStats& stats() const { return m_stats; }
    const std::map<int, H264SPS>& h264SpsMap() const { return m_h264SpsMap; }
    const std::map<int, H264PPS>& h264PpsMap() const { return m_h264PpsMap; }
    const std::map<int, H265SPS>& h265SpsMap() const { return m_h265SpsMap; }
    const std::map<int, H265PPS>& h265PpsMap() const { return m_h265PpsMap; }
    const std::map<int, H265VPS>& h265VpsMap() const { return m_h265VpsMap; }

private:
    StreamStats m_stats;
    std::map<int, H264SPS> m_h264SpsMap;
    std::map<int, H264PPS> m_h264PpsMap;
    std::map<int, H265SPS> m_h265SpsMap;
    std::map<int, H265PPS> m_h265PpsMap;
    std::map<int, H265VPS> m_h265VpsMap;
};
