#include <iostream>
#include <string>

#include "comm/comm_handler.hpp"
#include "config.hpp"

enum class NodeType {SOURCE, PIPE, UTURN};

class CommNode
{
private:
    const NodeType type_;
    std::unique_ptr<ICommHandler> aSide, bSide;

    void BuildNode(NodeConfig & config);
public:
    CommNode(NodeConfig & config, NodeType nodeType);
    ~CommNode(){};

    void Send(const std::string & message);
};
