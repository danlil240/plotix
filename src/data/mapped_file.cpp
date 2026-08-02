#include "data/mapped_file.hpp"

#include <stdexcept>
#include <utility>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace spectra::data
{

MappedFile::MappedFile(const std::string& path)
#ifdef _WIN32
    : file_handle_(nullptr), mapping_handle_(nullptr)
#else
    : fd_(::open(path.c_str(), O_RDONLY))
#endif
{
#ifdef _WIN32
    file_handle_ = CreateFileA(path.c_str(),
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               nullptr,
                               OPEN_EXISTING,
                               0,
                               nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("MappedFile: cannot open " + path);

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file_handle_, &file_size))
    {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::runtime_error("MappedFile: cannot get size of " + path);
    }
    size_ = static_cast<std::size_t>(file_size.QuadPart);

    if (size_ == 0)
    {
        // Empty file: leave data_ as nullptr, valid "open" state.
        open_ = true;
        return;
    }

    mapping_handle_ = CreateFileMappingA(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_)
    {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
        throw std::runtime_error("MappedFile: cannot create file mapping for " + path);
    }

    data_ = MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0);
    if (!data_)
    {
        CloseHandle(mapping_handle_);
        CloseHandle(file_handle_);
        mapping_handle_ = nullptr;
        file_handle_    = nullptr;
        throw std::runtime_error("MappedFile: cannot map view of " + path);
    }
    open_ = true;
#else

    if (fd_ < 0)
        throw std::runtime_error("MappedFile: cannot open " + path);

    struct stat st
    {
    };
    if (::fstat(fd_, &st) != 0)
    {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("MappedFile: cannot stat " + path);
    }
    size_ = static_cast<std::size_t>(st.st_size);

    if (size_ == 0)
    {
        // Empty file: leave data_ as nullptr.
        open_ = true;
        return;
    }

    data_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (data_ == MAP_FAILED)
    {
        data_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("MappedFile: mmap failed for " + path);
    }
    open_ = true;
#endif
}

MappedFile::~MappedFile()
{
    close();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
      open_(std::exchange(other.open_, false))
#ifdef _WIN32
      ,
      file_handle_(std::exchange(other.file_handle_, nullptr)),
      mapping_handle_(std::exchange(other.mapping_handle_, nullptr))
#else
      ,
      fd_(std::exchange(other.fd_, -1))
#endif
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other)
    {
        close();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        open_ = std::exchange(other.open_, false);
#ifdef _WIN32
        file_handle_    = std::exchange(other.file_handle_, nullptr);
        mapping_handle_ = std::exchange(other.mapping_handle_, nullptr);
#else
        fd_ = std::exchange(other.fd_, -1);
#endif
    }
    return *this;
}

std::span<const float> MappedFile::subspan_float(std::size_t byte_offset, std::size_t count) const
{
    if (!data_ || byte_offset >= size_)
        return {};

    std::size_t avail_bytes = size_ - byte_offset;
    std::size_t max_floats  = avail_bytes / sizeof(float);
    count                   = std::min(count, max_floats);

    const auto* base = static_cast<const char*>(data_) + byte_offset;
    return {reinterpret_cast<const float*>(base), count};
}

void MappedFile::close()
{
#ifdef _WIN32
    if (data_)
    {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (mapping_handle_)
    {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_)
    {
        CloseHandle(file_handle_);
        file_handle_ = nullptr;
    }
#else
    if (data_ && size_ > 0)
    {
        ::munmap(data_, size_);
        data_ = nullptr;
    }
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
#endif
    size_ = 0;
    open_ = false;
}

}   // namespace spectra::data
