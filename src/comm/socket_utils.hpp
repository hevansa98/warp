#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include "comm/comm_handler.hpp"

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) noexcept : fd_(fd) {}
    ~Socket() { close(); }

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    Socket& operator=(Socket&& o) noexcept {
        if (this != &o) { close(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }

    [[nodiscard]] int  get()   const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    void close() noexcept { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }

private:
    int fd_{-1};
};

[[noreturn]] inline void throw_errno(const char* ctx) {
    throw std::system_error(errno, std::generic_category(), ctx);
}

inline sockaddr_in make_addr(std::string_view host, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
        return addr;
    }

    std::string h(host);
    if (::inet_pton(AF_INET, h.c_str(), &addr.sin_addr) == 1)
        return addr;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* res   = nullptr;
    if (::getaddrinfo(h.c_str(), nullptr, &hints, &res) != 0 || !res)
        throw std::runtime_error("Cannot resolve host: " + h);
    addr.sin_addr = reinterpret_cast<const sockaddr_in*>(res->ai_addr)->sin_addr;
    ::freeaddrinfo(res);
    return addr;
}

inline PeerInfo peer_info(const sockaddr_in& sa) {
    char buf[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
    return {buf, ntohs(sa.sin_port)};
}

// Returns true if fd is readable within timeout_us microseconds.
inline bool wait_readable(int fd, int timeout_us = 200'000) noexcept {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv{0, static_cast<suseconds_t>(timeout_us)};
    return ::select(fd + 1, &fds, nullptr, nullptr, &tv) > 0;
}
