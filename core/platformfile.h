#pragma once

#include <cstdint>
#include <string>

// Read-only file mapping with a portable interface: POSIX mmap on macOS/Linux,
// CreateFileMapping/MapViewOfFile on Windows.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
    MappedFile(MappedFile &&other) noexcept;
    MappedFile &operator=(MappedFile &&other) noexcept;

    bool open(const std::string &path, std::string &error);
    void close();
    void releaseResidentPages();

    const uint8_t *data() const { return m_data; }
    uint64_t size() const { return m_size; }

private:
#ifdef _WIN32
    void *m_file = nullptr;      // HANDLE
    void *m_mapping = nullptr;   // HANDLE
#else
    void *m_mapping = nullptr;   // mmap address
#endif
    const uint8_t *m_data = nullptr;
    uint64_t m_size = 0;
};
