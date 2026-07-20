#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "comm/comm_handler.hpp"
#include "config.hpp"
#include "message.pb.h"

enum class NodeType { SOURCE, PIPE, UTURN };

class CommNode
{
public:
    CommNode(NodeConfig& config, NodeType nodeType);
    ~CommNode() = default;

    void Send(const dist_comms::NodeMessage& message);

private:
    void BuildNode(NodeConfig& config);

    NodeType                                    type_;
    std::vector<std::unique_ptr<ICommHandler>>  handlers_;
};
