#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

enum class Protocol { TCP, UDP };

struct PeerInfo {
    std::string address;
    uint16_t    port{0};
};

// ─── Abstract interface ───────────────────────────────────────────────────────
//
// TCP and UDP share this surface so callers can swap transports without
// changing any other code.  All methods are thread-safe.
//
// Typical usage:
//   Client: connect() → send() / start_receive()
//   Server: bind()   → start_receive() / send()   (TCP: auto-accepts one peer)
//   UDP sender only: send_to() (no prior setup required)
//
class ICommHandler {
public:
    using Bytes     = std::vector<std::byte>;
    using ByteView  = std::span<const std::byte>;
    using OnReceive = std::function<void(Bytes data, PeerInfo peer)>;

    virtual ~ICommHandler() = default;

    // TCP: establishes connection.  UDP: sets default send destination.
    virtual void connect(std::string_view host, uint16_t port) = 0;

    // Bind to a local address (TCP: also calls listen()).
    virtual void bind(std::string_view host, uint16_t port) = 0;

    // Send to the connected / default peer.
    virtual void send(ByteView data) = 0;

    // Send to an explicit peer.  TCP ignores host/port (peer is already fixed).
    virtual void send_to(ByteView data, std::string_view host, uint16_t port) = 0;

    // Start a background receive thread.  Callback is invoked for every
    // received message; it may be called from the receive thread.
    virtual void start_receive(OnReceive cb) = 0;

    // Stop the receive thread and close the socket.  Blocks until the thread
    // exits (at most ~200 ms after running_ is cleared).
    virtual void stop() = 0;

    [[nodiscard]] virtual bool     is_connected() const noexcept = 0;
    [[nodiscard]] virtual Protocol protocol()     const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<ICommHandler> make_comm_handler(Protocol proto);
