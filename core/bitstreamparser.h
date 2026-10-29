#ifndef DIA_CORE_BITSTREAMPARSER_H_
#define DIA_CORE_BITSTREAMPARSER_H_

#include <functional>
#include <string>
#include <vector>

#include "nalunit.h"
#include "platformfile.h"

class BitstreamParser {
 public:
  explicit BitstreamParser(CodecType codec);
  ~BitstreamParser();

  BitstreamParser(const BitstreamParser&) = delete;
  BitstreamParser& operator=(const BitstreamParser&) = delete;
  BitstreamParser(BitstreamParser&& other) noexcept;
  BitstreamParser& operator=(BitstreamParser&& other) noexcept;

  // Progress callback: fraction in [0,1]; return false to abort.
  using ProgressFn = std::function<bool(double)>;

  bool ParseFile(const std::string& path, const ProgressFn& progress = {});
  bool ParseBuffer(const uint8_t* data, size_t len);

  // Parse a container (MP4/MOV/MKV) directly from length-prefixed NAL units
  // (AVCC/hvcC), without converting to a temporary Annex B file. NAL offsets
  // refer to the original file.
  bool ParseAvcc(const std::string& path, CodecType& codec,
                  const ProgressFn& progress = {});

  // Fast path for plain MP4 files: reads the sample table in the moov box and
  // only touches NAL headers in the media data instead of demuxing every
  // payload byte. Returns false for fragmented/unsupported files so the
  // caller can fall back to ParseAvcc().
  bool ParseMp4Indexed(const std::string& path, CodecType& codec,
                         const ProgressFn& progress = {});

  const std::vector<NALUnit>& NalUnits() const { return nals_; }
  std::vector<NALUnit>& NalUnitsMut() { return nals_; }
  const std::string& LastError() const { return error_; }

  // Pointer to the first byte (start code) of a NAL unit inside the file
  // mapping / buffer. Valid until the next parse or destruction.
  const uint8_t* NalData(const NALUnit& nal) const {
    const uint64_t mapped = mapped_.size();
    if (mapped > 0 && nal.offset >= mapped)
      return extra_.empty() ? nullptr : extra_.data() + (nal.offset - mapped);
    return base_ ? base_ + nal.offset : nullptr;
  }

  // Drop resident pages of the file mapping (they are re-read on demand when
  // a NAL unit is inspected later). Keeps idle memory low after a big load.
  void ReleaseMappedPages();

  static CodecType DetectCodec(const uint8_t* data, size_t len);

 private:
  bool scan(const uint8_t* data, size_t len, const ProgressFn& progress);
  void ResetStorage();

  CodecType codec_;
  std::vector<NALUnit> nals_;
  std::string error_;
  std::vector<uint8_t> buffer_;  // owned copy when parsing a memory buffer
  std::vector<uint8_t> extra_;   // extradata NALs (avcC/hvcC)
  MappedFile mapped_;            // file mapping when parsing a path
  const uint8_t* base_ = nullptr;
};

#endif  // DIA_CORE_BITSTREAMPARSER_H_
