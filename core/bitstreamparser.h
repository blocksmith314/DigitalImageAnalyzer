#pragma once

#include "nalunit.h"
#include "platformfile.h"
#include <functional>
#include <string>
#include <vector>

class BitstreamParser {
public:
    explicit BitstreamParser(CodecType codec);
    ~BitstreamParser();

    BitstreamParser(const BitstreamParser &) = delete;
    BitstreamParser &operator=(const BitstreamParser &) = delete;
    BitstreamParser(BitstreamParser &&other) noexcept;
    BitstreamParser &operator=(BitstreamParser &&other) noexcept;

    // Progress callback: fraction in [0,1]; return false to abort.
    using ProgressFn = std::function<bool(double)>;

    bool parseFile(const std::string &path, const ProgressFn &progress = {});
    bool parseBuffer(const uint8_t *data, size_t len);

    // Parse a container (MP4/MOV/MKV) directly from length-prefixed NAL units
    // (AVCC/hvcC), without converting to a temporary Annex B file. NAL offsets
    // refer to the original file.
    bool parseAvcc(const std::string &path, CodecType &codec,
                   const ProgressFn &progress = {});

    // Fast path for plain MP4 files: reads the sample table in the moov box and
    // only touches NAL headers in the media data instead of demuxing every
    // payload byte. Returns false for fragmented/unsupported files so the
    // caller can fall back to parseAvcc().
    bool parseMp4Indexed(const std::string &path, CodecType &codec,
                         const ProgressFn &progress = {});

    const std::vector<NALUnit> &nalUnits() const { return m_nals; }
    std::vector<NALUnit> &nalUnitsMut() { return m_nals; }
    const std::string &lastError() const { return m_error; }

    // Pointer to the first byte (start code) of a NAL unit inside the file
    // mapping / buffer. Valid until the next parse or destruction.
    const uint8_t *nalData(const NALUnit &nal) const {
        const uint64_t mapped = m_mapped.size();
        if (mapped > 0 && nal.offset >= mapped)
            return m_extra.empty() ? nullptr : m_extra.data() + (nal.offset - mapped);
        return m_base ? m_base + nal.offset : nullptr;
    }

    // Drop resident pages of the file mapping (they are re-read on demand when
    // a NAL unit is inspected later). Keeps idle memory low after a big load.
    void releaseMappedPages();

    static CodecType detectCodec(const uint8_t *data, size_t len);

private:
    bool scan(const uint8_t *data, size_t len, const ProgressFn &progress);
    void resetStorage();

    CodecType m_codec;
    std::vector<NALUnit> m_nals;
    std::string m_error;
    std::vector<uint8_t> m_buffer;   // owned copy when parsing a memory buffer
    std::vector<uint8_t> m_extra;    // extradata NALs (avcC/hvcC)
    MappedFile m_mapped;             // file mapping when parsing a path
    const uint8_t *m_base = nullptr;
};
