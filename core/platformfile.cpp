#include "platformfile.h"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : Data(other.Data), size_(other.size_) {
#ifdef _WIN32
  file_ = other.file_;
  mapping_ = other.mapping_;
  other.file_ = nullptr;
  other.mapping_ = nullptr;
#else
  mapping_ = other.mapping_;
  other.mapping_ = nullptr;
#endif
  other.Data = nullptr;
  other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    close();
    Data = other.Data;
    size_ = other.size_;
#ifdef _WIN32
    file_ = other.file_;
    mapping_ = other.mapping_;
    other.file_ = nullptr;
    other.mapping_ = nullptr;
#else
    mapping_ = other.mapping_;
    other.mapping_ = nullptr;
#endif
    other.Data = nullptr;
    other.size_ = 0;
  }
  return *this;
}

bool MappedFile::open(const std::string& path, std::string& error) {
  close();

#ifdef _WIN32
  HANDLE file =
      CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = "Cannot open file: " + path;
    return false;
  }
  LARGE_INTEGER size;
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
    CloseHandle(file);
    error = "Cannot stat file or file is empty";
    return false;
  }
  HANDLE mapping =
      CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (!mapping) {
    CloseHandle(file);
    error = "Cannot map file into memory";
    return false;
  }
  void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (!view) {
    CloseHandle(mapping);
    CloseHandle(file);
    error = "Cannot map file into memory";
    return false;
  }
  file_ = file;
  mapping_ = mapping;
  Data = static_cast<const uint8_t*>(view);
  size_ = (uint64_t)size.QuadPart;
  return true;
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    error = "Cannot open file: " + path;
    return false;
  }
  struct stat st{};
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    error = "Cannot stat file or file is empty";
    return false;
  }
  void* map =
      ::mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd);
  if (map == MAP_FAILED) {
    error = "Cannot map file into memory";
    return false;
  }
  mapping_ = map;
  Data = static_cast<const uint8_t*>(map);
  size_ = (uint64_t)st.st_size;
  return true;
#endif
}

void MappedFile::close() {
#ifdef _WIN32
  if (Data) UnmapViewOfFile(Data);
  if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
  if (file_) CloseHandle(static_cast<HANDLE>(file_));
  file_ = nullptr;
  mapping_ = nullptr;
#else
  if (mapping_) munmap(mapping_, (size_t)size_);
  mapping_ = nullptr;
#endif
  Data = nullptr;
  size_ = 0;
}

void MappedFile::ReleaseResidentPages() {
  if (!Data || size_ == 0) return;
#ifndef _WIN32
  madvise(mapping_, (size_t)size_, MADV_DONTNEED);
#endif
}
