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

MappedFile::~MappedFile() {
    close();
}

MappedFile::MappedFile(MappedFile &&other) noexcept
    : m_data(other.m_data), m_size(other.m_size) {
#ifdef _WIN32
    m_file = other.m_file;
    m_mapping = other.m_mapping;
    other.m_file = nullptr;
    other.m_mapping = nullptr;
#else
    m_mapping = other.m_mapping;
    other.m_mapping = nullptr;
#endif
    other.m_data = nullptr;
    other.m_size = 0;
}

MappedFile &MappedFile::operator=(MappedFile &&other) noexcept {
    if (this != &other) {
        close();
        m_data = other.m_data;
        m_size = other.m_size;
#ifdef _WIN32
        m_file = other.m_file;
        m_mapping = other.m_mapping;
        other.m_file = nullptr;
        other.m_mapping = nullptr;
#else
        m_mapping = other.m_mapping;
        other.m_mapping = nullptr;
#endif
        other.m_data = nullptr;
        other.m_size = 0;
    }
    return *this;
}

bool MappedFile::open(const std::string &path, std::string &error) {
    close();

#ifdef _WIN32
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
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
    HANDLE mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        CloseHandle(file);
        error = "Cannot map file into memory";
        return false;
    }
    void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(mapping);
        CloseHandle(file);
        error = "Cannot map file into memory";
        return false;
    }
    m_file = file;
    m_mapping = mapping;
    m_data = static_cast<const uint8_t *>(view);
    m_size = (uint64_t)size.QuadPart;
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        error = "Cannot open file: " + path;
        return false;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        error = "Cannot stat file or file is empty";
        return false;
    }
    void *map = ::mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        error = "Cannot map file into memory";
        return false;
    }
    m_mapping = map;
    m_data = static_cast<const uint8_t *>(map);
    m_size = (uint64_t)st.st_size;
    return true;
#endif
}

void MappedFile::close() {
#ifdef _WIN32
    if (m_data) UnmapViewOfFile(m_data);
    if (m_mapping) CloseHandle(static_cast<HANDLE>(m_mapping));
    if (m_file) CloseHandle(static_cast<HANDLE>(m_file));
    m_file = nullptr;
    m_mapping = nullptr;
#else
    if (m_mapping) munmap(m_mapping, (size_t)m_size);
    m_mapping = nullptr;
#endif
    m_data = nullptr;
    m_size = 0;
}

void MappedFile::releaseResidentPages() {
    if (!m_data || m_size == 0) return;
#ifndef _WIN32
    madvise(m_mapping, (size_t)m_size, MADV_DONTNEED);
#endif
}
