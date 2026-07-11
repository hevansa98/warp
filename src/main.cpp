#include <atomic>
#include <chrono>
#include <csignal>
#include <span>
#include <stdexcept>
#include <thread>

#include "node/node.hpp"
#include "message.pb.h"

static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running = false; }

static NodeType node_type_from_string(const std::string& s) {
    if (s == "source") return NodeType::SOURCE;
    if (s == "pipe")   return NodeType::PIPE;
    if (s == "uturn")  return NodeType::UTURN;
    throw std::runtime_error("Unknown node type: " + s);
}

int main(int argc, char* argv[])
{
    if (argc < 2) throw std::runtime_error("Usage: dist_comms <config.yaml>");

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    auto config   = parse_config(argv[1]);
    auto nodeType = node_type_from_string(config.type);
    CommNode node(config, nodeType);

    if (argc == 3)
    {
        dist_comms::NodeMessage msg;
        msg.set_payload("hello!");

        std::string buf;
        msg.SerializeToString(&buf);
        node.Send(buf);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    while (g_running && (nodeType != NodeType::SOURCE)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // node destructor calls stop() on both sides
    return 0;
}
