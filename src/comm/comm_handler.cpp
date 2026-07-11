#include "comm/comm_handler.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace {

// RAII wrapper: closes the fd on destruction, move-only.
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

[[noreturn]] void throw_errno(const char* ctx) {
    throw std::system_error(errno, std::generic_category(), ctx);
}

sockaddr_in make_addr(std::string_view host, uint16_t port) {
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

PeerInfo peer_info(const sockaddr_in& sa) {
    char buf[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
    return {buf, ntohs(sa.sin_port)};
}

// Block until fd is readable or timeout_us elapses; returns true if readable.
bool wait_readable(int fd, int timeout_us = 200'000) noexcept {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeval tv{0, static_cast<suseconds_t>(timeout_us)};
    return ::select(fd + 1, &fds, nullptr, nullptr, &tv) > 0;
}

} // namespace

// ─── TcpHandler ──────────────────────────────────────────────────────────────

class TcpHandler final : public ICommHandler {
public:
    ~TcpHandler() override { stop(); }

    void connect(std::string_view host, uint16_t port) override {
        std::lock_guard lock(mu_);
        Socket s(::socket(AF_INET, SOCK_STREAM, 0));
        if (!s.valid()) throw_errno("socket");
        sockaddr_in addr = make_addr(host, port);
        if (::connect(s.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw_errno("connect");
        sock_      = std::move(s);
        server_    = false;
        connected_ = true;
    }

    void bind(std::string_view host, uint16_t port) override {
        std::lock_guard lock(mu_);
        Socket s(::socket(AF_INET, SOCK_STREAM, 0));
        if (!s.valid()) throw_errno("socket");
        int yes = 1;
        ::setsockopt(s.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr = make_addr(host, port);
        if (::bind(s.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw_errno("bind");
        if (::listen(s.get(), 16) < 0)
            throw_errno("listen");
        sock_   = std::move(s);
        server_ = true;
    }

    void send(ByteView data) override {
        std::lock_guard lock(mu_);
        raw_send(data);
    }

    void send_to(ByteView data, std::string_view, uint16_t) override {
        // TCP: peer is fixed at connect time; host/port args are ignored.
        send(data);
    }

    void start_receive(OnReceive cb) override {
        stop_thread();
        bool is_server;
        int  listen_fd;
        {
            std::lock_guard lock(mu_);
            is_server = server_;
            listen_fd = sock_.get();
        }
        running_ = true;
        thread_  = std::thread([this, is_server, listen_fd, cb = std::move(cb)]() mutable {
            is_server ? accept_loop(listen_fd, cb) : recv_loop(cb);
        });
    }

    void stop() override { stop_thread(); }

    [[nodiscard]] bool     is_connected() const noexcept override { return connected_; }
    [[nodiscard]] Protocol protocol()     const noexcept override { return Protocol::TCP; }

private:
    // Caller must hold mu_.
    void raw_send(ByteView data) {
        int fd = server_ ? peer_.get() : sock_.get();
        if (fd < 0) throw std::runtime_error("TCP: not connected");
        const auto* p    = data.data();
        ssize_t     left = static_cast<ssize_t>(data.size());
        while (left > 0) {
            ssize_t n = ::send(fd, p, static_cast<size_t>(left), MSG_NOSIGNAL);
            if (n < 0) throw_errno("send");
            p    += n;
            left -= n;
        }
    }

    void stop_thread() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        std::lock_guard lock(mu_);
        peer_.close();
        sock_.close();
        connected_ = false;
    }

    // Client-mode receive: read from the connected socket.
    void recv_loop(const OnReceive& cb) {
        constexpr std::size_t BUF = 65'536;
        Bytes buf(BUF);

        int fd;
        { std::lock_guard lock(mu_); fd = sock_.get(); }

        while (running_) {
            if (!wait_readable(fd)) continue;
            ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            sockaddr_in sa{}; socklen_t sl = sizeof(sa);
            ::getpeername(fd, reinterpret_cast<sockaddr*>(&sa), &sl);
            cb(Bytes(buf.begin(), buf.begin() + n), peer_info(sa));
        }
        connected_ = false;
    }

    // Server-mode: accept one client at a time, read until it disconnects,
    // then wait for the next client.
    void accept_loop(int listen_fd, const OnReceive& cb) {
        constexpr std::size_t BUF = 65'536;
        Bytes buf(BUF);

        while (running_) {
            if (!wait_readable(listen_fd)) continue;

            sockaddr_in sa{}; socklen_t sl = sizeof(sa);
            int cfd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&sa), &sl);
            if (cfd < 0) continue;

            auto pi = peer_info(sa);
            {
                std::lock_guard lock(mu_);
                peer_      = Socket(cfd);
                connected_ = true;
            }

            while (running_) {
                if (!wait_readable(cfd)) continue;
                ssize_t n = ::recv(cfd, buf.data(), buf.size(), 0);
                if (n <= 0) break;
                cb(Bytes(buf.begin(), buf.begin() + n), pi);
            }

            {
                std::lock_guard lock(mu_);
                peer_.close();
                connected_ = false;
            }
        }
    }

    mutable std::mutex mu_;
    Socket             sock_;
    Socket             peer_;        // server mode: currently accepted client
    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    bool               server_{false};
    std::thread        thread_;
};

// ─── UdpHandler ──────────────────────────────────────────────────────────────

class UdpHandler final : public ICommHandler {
public:
    ~UdpHandler() override { stop(); }

    // Sets the default destination for send(); does not block.
    void connect(std::string_view host, uint16_t port) override {
        std::lock_guard lock(mu_);
        ensure_socket();
        default_peer_ = make_addr(host, port);
        has_peer_     = true;
        connected_    = true;
    }

    void bind(std::string_view host, uint16_t port) override {
        std::lock_guard lock(mu_);
        ensure_socket();
        sockaddr_in addr = make_addr(host, port);
        if (::bind(sock_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw_errno("bind");
    }

    void send(ByteView data) override {
        std::lock_guard lock(mu_);
        if (!has_peer_) throw std::runtime_error("UDP: no peer — call connect() or send_to()");
        raw_sendto(data, default_peer_);
    }

    void send_to(ByteView data, std::string_view host, uint16_t port) override {
        std::lock_guard lock(mu_);
        ensure_socket();
        raw_sendto(data, make_addr(host, port));
    }

    void start_receive(OnReceive cb) override {
        stop_thread();
        int fd;
        {
            std::lock_guard lock(mu_);
            ensure_socket();
            fd = sock_.get();
        }
        running_ = true;
        thread_  = std::thread([this, fd, cb = std::move(cb)]() mutable {
            constexpr std::size_t BUF = 65'536;
            Bytes buf(BUF);

            while (running_) {
                if (!wait_readable(fd)) continue;
                sockaddr_in sa{}; socklen_t sl = sizeof(sa);
                ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0,
                                       reinterpret_cast<sockaddr*>(&sa), &sl);
                if (n < 0) break;
                cb(Bytes(buf.begin(), buf.begin() + n), peer_info(sa));
            }
        });
    }

    void stop() override { stop_thread(); }

    [[nodiscard]] bool     is_connected() const noexcept override { return connected_; }
    [[nodiscard]] Protocol protocol()     const noexcept override { return Protocol::UDP; }

private:
    // Caller must hold mu_.
    void ensure_socket() {
        if (sock_.valid()) return;
        Socket s(::socket(AF_INET, SOCK_DGRAM, 0));
        if (!s.valid()) throw_errno("socket");
        sock_ = std::move(s);
    }

    // Caller must hold mu_.
    void raw_sendto(ByteView data, const sockaddr_in& dest) {
        ssize_t n = ::sendto(sock_.get(), data.data(), data.size(), 0,
                             reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        if (n < 0) throw_errno("sendto");
    }

    void stop_thread() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        // Thread is dead — safe to reset state without the lock.
        connected_ = false;
    }

    mutable std::mutex mu_;
    Socket             sock_;
    sockaddr_in        default_peer_{};
    bool               has_peer_{false};
    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    std::thread        thread_;
};

// ─── Factory ─────────────────────────────────────────────────────────────────

std::unique_ptr<ICommHandler> make_comm_handler(Protocol proto) {
    switch (proto) {
        case Protocol::TCP: return std::make_unique<TcpHandler>();
        case Protocol::UDP: return std::make_unique<UdpHandler>();
    }
    throw std::invalid_argument("make_comm_handler: unknown protocol");
}
