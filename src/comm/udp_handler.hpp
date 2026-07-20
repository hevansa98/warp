#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "socket_utils.hpp"

class UdpHandler final : public ICommHandler {
public:
    ~UdpHandler() override;

    void connect(std::string_view host, uint16_t port) override;
    void bind(std::string_view host, uint16_t port) override;
    void send(ByteView data) override;
    void send_to(ByteView data, std::string_view host, uint16_t port) override;
    void start_receive(OnReceive cb) override;
    void stop() override;

    [[nodiscard]] bool     is_connected() const noexcept override;
    [[nodiscard]] Protocol protocol()     const noexcept override;

private:
    void ensure_socket();                                    // caller must hold mu_
    void raw_sendto(ByteView data, const sockaddr_in& dest); // caller must hold mu_
    void stop_thread();

    mutable std::mutex mu_;
    Socket             sock_;
    sockaddr_in        default_peer_{};
    bool               has_peer_{false};
    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    std::thread        thread_;
};
