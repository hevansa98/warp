#include "node/node.hpp"

CommNode::CommNode(NodeConfig & config, NodeType nodeType) : type_(nodeType)
{
    switch (type_)
    {
    case NodeType::SOURCE:
        bSide = make_comm_handler(Protocol::UDP);
        break;
    case NodeType::PIPE:
        aSide = make_comm_handler(Protocol::UDP);
        bSide = make_comm_handler(Protocol::UDP);
        break;

    case NodeType::UTURN:
        aSide = make_comm_handler(Protocol::UDP);
        break;
    
    default:
        break;
    }

    BuildNode(config);
}

void CommNode::BuildNode(NodeConfig & config)
{
    switch (type_)
    {
    case NodeType::SOURCE:
        bSide->bind(config.ip, config.portb);
        bSide->connect(config.ip, config.portbdest);
        bSide->start_receive(
            [&](ICommHandler::Bytes data, PeerInfo peer){
            std::cout << "SENDER: " << peer.address << ":" << peer.port << "\n";
            std::cout << "RECEIVED PAYLOAD\n";
        });
        break;
    case NodeType::PIPE:
        aSide->bind(config.ip, config.porta);
        aSide->connect(config.ip, config.portadest);
        aSide->start_receive(
            [&](ICommHandler::Bytes data, PeerInfo peer){
            std::cout << "SENDER: " << peer.address << ":" << peer.port << "\n";
            std::cout << "PASSING THROUGH TO B SIDE\n";
            bSide->send_to(std::as_bytes(std::span{data}), config.ip, config.portbdest);
        });

        bSide->bind(config.ip, config.portb);
        bSide->connect(config.ip, config.portbdest);
        bSide->start_receive(
            [&](ICommHandler::Bytes data, PeerInfo peer){
            std::cout << "SENDER: " << peer.address << ":" << peer.port << "\n";
            std::cout << "PASSING THROUGH TO A SIDE\n";
            aSide->send_to(std::as_bytes(std::span{data}), config.ip, config.portadest);
        });
        break;

    case NodeType::UTURN:
        aSide->bind(config.ip, config.porta);
        aSide->start_receive(
            [&](ICommHandler::Bytes data, PeerInfo peer){
            std::cout << "SENDER: " << peer.address << ":" << peer.port << "\n";
            std::cout << "RETURNING TO SENDER\n";
            aSide->send_to(std::as_bytes(std::span{data}), peer.address, peer.port);
        });
        break;
    
    default:
        break;
    }
}

void CommNode::Send(const std::string & message)
{
    bSide->send(std::as_bytes(std::span{message}));
}