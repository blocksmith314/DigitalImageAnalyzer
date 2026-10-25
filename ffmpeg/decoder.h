#pragma once
#include "nalunit.h"
#include <QImage>
#include <string>
#include <vector>
#include <unordered_map>

struct DecodedFrame {
    int64_t pts = 0;
    double ptsSec = 0.0;   // presentation time in seconds
    int width = 0;
    int height = 0;
    QImage image;
};

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool openFile(const std::string &path, CodecType codec);
    void close();

    void reset();
    bool decodeNextFrame(DecodedFrame &out);
    bool seekToFrame(int targetIndex);
    bool seekByContainer(int targetIndex);

    bool decodeKeyframes(std::vector<DecodedFrame> &out, int maxFrames = 10);

    std::unordered_map<int64_t, int> buildPktPosToDisplayIndexMap();
    std::unordered_map<int64_t, int> buildPosToDisplayIndexMapFromNals(const std::vector<NALUnit> &nals);

    bool hasTimestamps() const;
    int frameCount() const { return m_frameCount; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double frameRate() const { return m_frameRate; }
    int currentFrameIndex() const { return m_currentFrame; }
    const std::string &lastError() const { return m_error; }

private:
    struct Impl;
    Impl *m_impl = nullptr;
    int m_frameCount = 0;
    int m_width = 0;
    int m_height = 0;
    int m_currentFrame = 0;
    double m_frameRate = 30.0;
    std::string m_path;
    CodecType m_codec = CodecType::H264;
    std::string m_error;
};