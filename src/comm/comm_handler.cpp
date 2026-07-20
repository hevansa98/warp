#include "tcp_handler.hpp"
#include "udp_handler.hpp"

std::unique_ptr<ICommHandler> make_comm_handler(Protocol proto) {
    switch (proto) {
        case Protocol::TCP: return std::make_unique<TcpHandler>();
        case Protocol::UDP: return std::make_unique<UdpHandler>();
    }
    throw std::invalid_argument("make_comm_handler: unknown protocol");
}
