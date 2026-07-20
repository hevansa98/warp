#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "socket_utils.hpp"

class TcpHandler final : public ICommHandler {
public:
    ~TcpHandler() override;

    void connect(std::string_view host, uint16_t port) override;
    void bind(std::string_view host, uint16_t port) override;
    void send(ByteView data) override;
    void send_to(ByteView data, std::string_view host, uint16_t port) override;
    void start_receive(OnReceive cb) override;
    void stop() override;

    [[nodiscard]] bool     is_connected() const noexcept override;
    [[nodiscard]] Protocol protocol()     const noexcept override;

private:
    void raw_send(ByteView data);  // caller must hold mu_
    void stop_thread();
    void recv_loop(const OnReceive& cb);
    void accept_loop(int listen_fd, const OnReceive& cb);

    mutable std::mutex mu_;
    Socket             sock_;
    Socket             peer_;       // server mode: currently accepted client
    std::atomic<bool>  running_{false};
    std::atomic<bool>  connected_{false};
    bool               server_{false};
    std::thread        thread_;
};
