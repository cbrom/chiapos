// Copyright 2018 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_CPP_DISK_UTIL_HPP_
#define SRC_CPP_DISK_UTIL_HPP_

#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <memory>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
#include <sys/sysmacros.h>
#endif

#include "chia_filesystem.hpp"
#include "util.hpp"

namespace DiskUtil {

#if !defined(__APPLE__) && !defined(_WIN32)
    inline fs::path DevicePath(dev_t dev_id)
    {
        dev_t dev_id_major = major(dev_id);
        dev_t dev_id_minor = minor(dev_id);

        std::ostringstream os;
        os << "/sys/dev/block/" << dev_id_major << ":" << dev_id_minor;
        std::string symlink = os.str();

        std::unique_ptr<char, decltype(free) *> pathbuf {
            realpath(symlink.c_str(), nullptr), free };
        if (!pathbuf) {
            std::ostringstream err;
            std::cerr << "Unable to find full device path: "
                << strerror(errno) << std::endl;
            return fs::path();
        }

        return fs::path(pathbuf.get());
    }
#endif

    inline bool IsRotational(const std::string &dir)
    {
#if defined(__APPLE__) || defined(_WIN32)
        return false;
#else
        struct stat s{};

        if (0 != stat(dir.c_str(), &s)) {
            std::ostringstream err;
            std::cerr << "Unable to find device name for dir " << dir << ": "
                << strerror(errno) << std::endl;
            return false;
        }

        fs::path device_path = DevicePath(s.st_dev);
        if (device_path.empty()) {
            return false;
        }

        fs::path filename;
        for (;;) {
            filename = device_path / "queue" / "rotational";

            if (fs::exists(filename)) {
                break;
            }

            if (!device_path.has_parent_path()) {
                std::cerr << "Unable to determine device media type" << std::endl;
                return false;
            }
            device_path = device_path.parent_path();
        }

        std::ifstream file;
        file.open(filename.c_str()); // Note: File opened

        if (file.fail()) {
            std::ostringstream err;
            std::cerr << "Unable to open " << filename << " for reading: "
                << strerror(errno) << std::endl;
            return false;
        }

        std::string line;
        getline(file, line); // Note: File read

        file.close(); // Note: File close

        return !line.empty() && line.front() == '1';
#endif
    }

    inline bool ShouldLock(const std::string &dir) {
        return DiskUtil::IsRotational(dir);
    }

    inline int LockDirectory(
        std::string dirname)
    {
#ifdef _WIN32
        return -1;
#else
        int dir_fd = open(dirname.c_str(), O_RDONLY | O_NOCTTY); // Note: File opened
        if (dir_fd == -1) {
            std::cerr << "Unable to open directory for locking: " << dirname
                << ". Error: " << strerror(errno) << std::endl;
            return -1;
        }
        while (0 != flock(dir_fd, LOCK_EX | LOCK_NB)) {
            using namespace std::chrono_literals;
            if (EWOULDBLOCK == errno) {
                std::this_thread::sleep_for(10s);
            } else {
                std::cerr << "Unable to lock directory (retrying in 1 minute): "
                    << ". Error: " << strerror(errno) << std::endl;
                std::this_thread::sleep_for(60s);
            }
        }
        return dir_fd;
#endif
    }

    inline bool UnlockDirectory(
        int dir_fd,
        std::string dirname)
    {
#ifdef _WIN32
        return false;
#else
        if (-1 == flock(dir_fd, LOCK_UN)) {
            std::cerr << "Failed to unlock the directory: " << dirname
                << ". Error: " << strerror(errno) << std::endl;
            return false;
        }
        if (-1 == close(dir_fd)) {
            std::cerr << "Failed to close the directory during unlocking: " << dirname
                << ". Error: " << strerror(errno) << std::endl;
            return false;
        }
        return true;
#endif
	}
}

class DirectoryLock
{
public:
    DirectoryLock(const std::string &dirname, bool lock = true)
    {
        dirname_ = dirname;
        if (lock) {
            Lock();
        }
    }

    DirectoryLock(const DirectoryLock&) = delete;

    virtual ~DirectoryLock()
    {
        Unlock();
    }

    bool Lock()
    {
        if (fd_ == -1) {
            using namespace std::chrono;

            std::cout << "Acquiring directory lock: " << dirname_ << std::endl;

            steady_clock::time_point start = steady_clock::now();
            fd_ = DiskUtil::LockDirectory(dirname_);
            steady_clock::time_point end = steady_clock::now();

            std::cout << "Lock acquired (took "
                << duration_cast<seconds>(end - start).count()
                << " sec)" << std::endl;
        }
        return fd_ != -1;
    }
    
    bool Unlock()
    {
        if (fd_ == -1) {
            return false;
        }
        std::cout << "Releasing directory lock: " << dirname_ << std::endl;
        if (!DiskUtil::UnlockDirectory(fd_, dirname_)) {
            return false;
        }
        fd_ = -1;
        return true;
    }

private:
    int fd_ = -1;
    std::string dirname_;
};

#endif // SRC_CPP_DISK_UTIL_HPP_



