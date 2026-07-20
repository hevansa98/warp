#include "udp_handler.hpp"

UdpHandler::~UdpHandler() { stop(); }

void UdpHandler::connect(std::string_view host, uint16_t port) {
    std::lock_guard lock(mu_);
    ensure_socket();
    default_peer_ = make_addr(host, port);
    has_peer_     = true;
    connected_    = true;
}

void UdpHandler::bind(std::string_view host, uint16_t port) {
    std::lock_guard lock(mu_);
    ensure_socket();
    sockaddr_in addr = make_addr(host, port);
    if (::bind(sock_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw_errno("bind");
}

void UdpHandler::send(ByteView data) {
    std::lock_guard lock(mu_);
    if (!has_peer_) throw std::runtime_error("UDP: no peer — call connect() or send_to()");
    raw_sendto(data, default_peer_);
}

void UdpHandler::send_to(ByteView data, std::string_view host, uint16_t port) {
    std::lock_guard lock(mu_);
    ensure_socket();
    raw_sendto(data, make_addr(host, port));
}

void UdpHandler::start_receive(OnReceive cb) {
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

void UdpHandler::stop() { stop_thread(); }

bool     UdpHandler::is_connected() const noexcept { return connected_; }
Protocol UdpHandler::protocol()     const noexcept { return Protocol::UDP; }

void UdpHandler::ensure_socket() {
    if (sock_.valid()) return;
    Socket s(::socket(AF_INET, SOCK_DGRAM, 0));
    if (!s.valid()) throw_errno("socket");
    sock_ = std::move(s);
}

void UdpHandler::raw_sendto(ByteView data, const sockaddr_in& dest) {
    ssize_t n = ::sendto(sock_.get(), data.data(), data.size(), 0,
                         reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    if (n < 0) throw_errno("sendto");
}

void UdpHandler::stop_thread() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    // Thread is dead — safe to reset state without the lock.
    connected_ = false;
}
