#pragma once

#include "nalunit.h"
#include <string>
#include <vector>

class BitstreamParser {
public:
    explicit BitstreamParser(CodecType codec);
    ~BitstreamParser();

    bool parseFile(const std::string &path);
    bool parseBuffer(const uint8_t *data, size_t len);

    const std::vector<NALUnit> &nalUnits() const { return m_nals; }
    const std::string &lastError() const { return m_error; }

    static CodecType detectCodec(const uint8_t *data, size_t len);

private:
    CodecType m_codec;
    std::vector<NALUnit> m_nals;
    std::string m_error;
};