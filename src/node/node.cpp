#include "node/node.hpp"

CommNode::CommNode(NodeConfig& config, NodeType nodeType) : type_(nodeType)
{
    for (std::size_t i = 0; i < config.endpoints.size(); ++i)
        handlers_.push_back(make_comm_handler(Protocol::UDP));

    BuildNode(config);
}

void CommNode::BuildNode(NodeConfig& config)
{
    const auto n = config.endpoints.size();

    switch (type_)
    {
    case NodeType::SOURCE:
        for (std::size_t i = 0; i < n; ++i) {
            const auto& ep = config.endpoints[i];
            handlers_[i]->bind(ep.source.ip, ep.source.port);
            handlers_[i]->connect(ep.destination.ip, ep.destination.port);
            handlers_[i]->start_receive([i](ICommHandler::Bytes data, PeerInfo peer) {
                dist_comms::NodeMessage msg;
                if (!msg.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
                    std::cout << "[source:" << i << "] failed to parse message\n";
                    return;
                }
                std::cout << "[source:" << i << "] from "
                          << peer.address << ":" << peer.port
                          << " seq=" << msg.seq()
                          << " payload=\"" << msg.payload() << "\"\n";
            });
        }
        break;

    case NodeType::PIPE:
        for (std::size_t i = 0; i < n; ++i) {
            const auto& ep   = config.endpoints[i];
            const auto  peer = (i + 1) % n;  // forward to next endpoint, wraps around
            handlers_[i]->bind(ep.source.ip, ep.source.port);
            handlers_[i]->start_receive([this, i, peer, &config](ICommHandler::Bytes data, PeerInfo from) {
                std::cout << "[pipe:" << i << "] from "
                          << from.address << ":" << from.port
                          << " → forwarding to endpoint " << peer << "\n";
                const auto& dest = config.endpoints[peer].destination;
                handlers_[peer]->send_to(std::as_bytes(std::span{data}), dest.ip, dest.port);
            });
        }
        break;

    case NodeType::UTURN:
        for (std::size_t i = 0; i < n; ++i) {
            const auto& ep = config.endpoints[i];
            handlers_[i]->bind(ep.source.ip, ep.source.port);
            handlers_[i]->start_receive([this, i](ICommHandler::Bytes data, PeerInfo peer) {
                std::cout << "[uturn:" << i << "] returning to "
                          << peer.address << ":" << peer.port << "\n";
                handlers_[i]->send_to(std::as_bytes(std::span{data}), peer.address, peer.port);
            });
        }
        break;

    default:
        break;
    }
}

void CommNode::Send(const dist_comms::NodeMessage& message)
{
    if (handlers_.empty()) return;
    std::string buf;
    message.SerializeToString(&buf);
    handlers_[0]->send(std::as_bytes(std::span{buf}));
}
