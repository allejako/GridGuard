#pragma once

#include <unistd.h>

namespace gridguard {

// RAII wrapper for POSIX file descriptors.
// Guarantees the fd is closed when the guard goes out of scope,
// even when exceptions are thrown. Ownership is unique — copy is disabled,
// move is supported (Rule of Five).
class SocketGuard {
public:
    explicit SocketGuard(int fd = -1) noexcept : fd_(fd) {}

    ~SocketGuard() {
        reset();
    }

    // Move constructor — transfers ownership
    SocketGuard(SocketGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    // Move assignment — close current, take ownership of other
    SocketGuard& operator=(SocketGuard&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // No copy — fd ownership must be unique
    SocketGuard(const SocketGuard&)            = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;

    int  get()   const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

    // Release ownership without closing (caller takes responsibility)
    int release() noexcept {
        int fd = fd_;
        fd_    = -1;
        return fd;
    }

    // Close current fd and optionally take a new one
    void reset(int newFd = -1) noexcept {
        if (fd_ >= 0) close(fd_);
        fd_ = newFd;
    }

private:
    int fd_;
};

} // namespace gridguard
