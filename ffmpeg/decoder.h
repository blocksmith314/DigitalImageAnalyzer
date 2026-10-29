#pragma once
#include <QImage>
#include <string>
#include <unordered_map>
#include <vector>
#include "nalunit.h"

struct AVFrame;

struct DecodedFrame
{
    int64_t pts = 0;
    double ptsSec = 0.0; // presentation time in seconds
    int width = 0;
    int height = 0;
    QImage image;
};

class FFmpegDecoder
{
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    bool openFile(const std::string& path, CodecType codec);
    void close();

    void reset();
    bool decodeNextFrame(DecodedFrame& out);
    // convert == false only decodes timestamps (used while seeking).
    bool decodeNextFrame(DecodedFrame& out, bool convert);
    bool seekToFrame(int targetIndex);
    bool seekByContainer(int targetIndex);

    bool decodeKeyframes(std::vector<DecodedFrame>& out, int maxFrames = 10);

    bool hasTimestamps() const;
    int frameCount() const { return m_frameCount; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double frameRate() const { return m_frameRate; }
    int currentFrameIndex() const { return m_currentFrame; }
    const std::string& lastError() const { return m_error; }

private:
    bool convertFrame(const AVFrame* frame, DecodedFrame& out);

    struct Impl;
    Impl* m_impl = nullptr;
    int m_frameCount = 0;
    int m_width = 0;
    int m_height = 0;
    int m_currentFrame = 0;
    double m_frameRate = 30.0;
    std::string m_path;
    CodecType m_codec = CodecType::H264;
    std::string m_error;
};
