#include "tcp_handler.hpp"

TcpHandler::~TcpHandler() { stop(); }

void TcpHandler::connect(std::string_view host, uint16_t port) {
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

void TcpHandler::bind(std::string_view host, uint16_t port) {
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

void TcpHandler::send(ByteView data) {
    std::lock_guard lock(mu_);
    raw_send(data);
}

void TcpHandler::send_to(ByteView data, std::string_view, uint16_t) {
    // TCP: peer is fixed at connect time; host/port args are ignored.
    send(data);
}

void TcpHandler::start_receive(OnReceive cb) {
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

void TcpHandler::stop() { stop_thread(); }

bool     TcpHandler::is_connected() const noexcept { return connected_; }
Protocol TcpHandler::protocol()     const noexcept { return Protocol::TCP; }

void TcpHandler::raw_send(ByteView data) {
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

void TcpHandler::stop_thread() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mu_);
    peer_.close();
    sock_.close();
    connected_ = false;
}

void TcpHandler::recv_loop(const OnReceive& cb) {
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

void TcpHandler::accept_loop(int listen_fd, const OnReceive& cb) {
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
