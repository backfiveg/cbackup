#pragma once
#include <unistd.h>
#include <stdexcept>
#include <string>

namespace cbackup {

// RAII wrapper for file descriptors
class FdWrapper {
 public:
    explicit FdWrapper(int fd = -1) : fd_(fd) {}

    ~FdWrapper() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // Non-copyable
    FdWrapper(const FdWrapper&) = delete;
    FdWrapper& operator=(const FdWrapper&) = delete;

    // Movable
    FdWrapper(FdWrapper&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    FdWrapper& operator=(FdWrapper&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

    void reset(int fd = -1) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

    int release() {
        int tmp = fd_;
        fd_ = -1;
        return tmp;
    }

 private:
    int fd_;
};

}  // namespace cbackup
