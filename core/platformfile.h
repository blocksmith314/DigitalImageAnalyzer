#ifndef DIA_CORE_PLATFORMFILE_H_
#define DIA_CORE_PLATFORMFILE_H_

#include <cstdint>
#include <string>

// Read-only file mapping with a portable interface: POSIX mmap on macOS/Linux,
// CreateFileMapping/MapViewOfFile on Windows.
class MappedFile {
 public:
  MappedFile() = default;
  ~MappedFile();

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;

  bool open(const std::string& path, std::string& error);
  void close();
  void ReleaseResidentPages();

  const uint8_t* data() const { return Data; }
  uint64_t size() const { return size_; }

 private:
#ifdef _WIN32
  void* file_ = nullptr;     // HANDLE
  void* mapping_ = nullptr;  // HANDLE
#else
  void* mapping_ = nullptr;  // mmap address
#endif
  const uint8_t* Data = nullptr;
  uint64_t size_ = 0;
};

#endif  // DIA_CORE_PLATFORMFILE_H_
