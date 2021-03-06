/*******************************************************************************
 * thrill/core/file_io.hpp
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Alexander Noe <aleexnoe@gmail.com>
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef THRILL_CORE_FILE_IO_HEADER
#define THRILL_CORE_FILE_IO_HEADER

#include <thrill/common/logger.hpp>
#include <thrill/common/porting.hpp>
#include <thrill/common/system_exception.hpp>

#if defined(_MSC_VER)

#include <io.h>

#else

#include <unistd.h>

#endif

#include <string>
#include <utility>
#include <vector>

namespace thrill {
namespace core {

//! function which takes pathbase and replaces $$$ with worker and ### with
//! the file_part values.
std::string FillFilePattern(const std::string& pathbase,
                            size_t worker, size_t file_part);

// Returns true, if file at filepath is compressed (e.g, ends with
// '.[gz/bz2,xz,lzo]')
static inline
bool IsCompressed(const std::string& path) {
    return common::EndsWith(path, ".gz") ||
           common::EndsWith(path, ".bz2") ||
           common::EndsWith(path, ".xz") ||
           common::EndsWith(path, ".lzo") ||
           common::EndsWith(path, ".lz4");
}

using FileSizePair = std::pair<std::string, size_t>;

/*!
 * Adds a pair of filename and size prefixsum (in bytes) for all files in
 * the input path.
 *
 * \param path Input path
 */
std::vector<FileSizePair> GlobFileSizePrefixSum(const std::string& path);

/*!
 * Returns a vector of all files found by glob in the input path.
 *
 * \param path Input path
 */
std::vector<std::string> GlobFilePattern(const std::string& path);

/*!
 * Represents a POSIX system file via its file descriptor.
 */
class SysFile
{
    static const bool debug = false;

public:
    //! default constructor
    SysFile() : fd_(-1) { }

    /*!
     * Open file for reading and return file descriptor. Handles compressed
     * files by calling a decompressor in a pipe, like "cat $f | gzip -dc |" in
     * bash.
     *
     * \param path Path to open
     */
    static SysFile OpenForRead(const std::string& path);

    /*!
     * Open file for writing and return file descriptor. Handles compressed
     * files by calling a compressor in a pipe, like "| gzip -d > $f" in bash.
     *
     * \param path Path to open
     */
    static SysFile OpenForWrite(const std::string& path);

    //! non-copyable: delete copy-constructor
    SysFile(const SysFile&) = delete;
    //! non-copyable: delete assignment operator
    SysFile& operator = (const SysFile&) = delete;
    //! move-constructor
    SysFile(SysFile&& f)
        : fd_(f.fd_), pid_(f.pid_) {
        f.fd_ = -1, f.pid_ = 0;
    }
    //! move-assignment
    SysFile& operator = (SysFile&& f) {
        close();
        fd_ = f.fd_, pid_ = f.pid_;
        f.fd_ = -1, f.pid_ = 0;
        return *this;
    }

    //! POSIX write function.
    ssize_t write(const void* data, size_t count) {
        assert(fd_ >= 0);
#if defined(_MSC_VER)
        return ::_write(fd_, data, static_cast<unsigned>(count));
#else
        return ::write(fd_, data, count);
#endif
    }

    //! POSIX read function.
    ssize_t read(void* data, size_t count) {
        assert(fd_ >= 0);
#if defined(_MSC_VER)
        return ::_read(fd_, data, static_cast<unsigned>(count));
#else
        return ::read(fd_, data, count);
#endif
    }

    //! POSIX lseek function from current position.
    ssize_t lseek(off_t offset) {
        assert(fd_ >= 0);
        return ::lseek(fd_, offset, SEEK_CUR);
    }

    //! close the file descriptor
    void close();

    ~SysFile() {
        close();
    }

protected:
    //! protected constructor: use OpenForRead or OpenForWrite.
    explicit SysFile(int fd, int pid = 0)
        : fd_(fd), pid_(pid) { }

    //! file descriptor
    int fd_ = -1;

#if defined(_MSC_VER)
    using pid_t = int;
#endif

    //! pid of child process to wait for
    pid_t pid_ = 0;
};

/*!
 * A class which creates a temporary directory in the current directory and
 * returns it via get(). When the object is destroyed the temporary directory is
 * wiped non-recursively.
 */
class TemporaryDirectory
{
public:
    //! Create a temporary directory, returns its name without trailing /.
    static std::string make_directory(const char* sample = "thrill-testsuite-");

    //! wipe temporary directory NON RECURSIVELY!
    static void wipe_directory(const std::string& tmp_dir, bool do_rmdir);

    TemporaryDirectory()
        : dir_(make_directory())
    { }

    ~TemporaryDirectory() {
        wipe_directory(dir_, true);
    }

    //! non-copyable: delete copy-constructor
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    //! non-copyable: delete assignment operator
    TemporaryDirectory& operator = (const TemporaryDirectory&) = delete;

    //! return the temporary directory name
    const std::string & get() const { return dir_; }

    //! wipe contents of directory
    void wipe() const {
        wipe_directory(dir_, false);
    }

protected:
    std::string dir_;
};

} // namespace core
} // namespace thrill

#endif // !THRILL_CORE_FILE_IO_HEADER

/******************************************************************************/
