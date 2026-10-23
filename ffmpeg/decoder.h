#pragma once

#include "nalunit.h"
#include <QImage>
#include <string>
#include <vector>

struct DecodedFrame {
    int64_t pts = 0;
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
    bool decodeNextFrame(DecodedFrame &out);
    bool decodeKeyframes(std::vector<DecodedFrame> &out, int maxFrames = 10);
    int frameCount() const { return m_frameCount; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    const std::string &lastError() const { return m_error; }

private:
    struct Impl;
    Impl *m_impl = nullptr;
    int m_frameCount = 0;
    int m_width = 0;
    int m_height = 0;
    std::string m_error;
};