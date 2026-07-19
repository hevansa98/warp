#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <yaml-cpp/yaml.h>

struct AddressConfig {
    std::string ip{"127.0.0.1"};
    uint16_t    port{0};
};

struct EndpointConfig {
    std::string   name;
    AddressConfig source;
    AddressConfig destination;
};

struct NodeConfig {
    std::string                 name;
    std::string                 type;
    std::vector<EndpointConfig> endpoints;
};

// Loads a YAML config file and returns a NodeConfig.
// Throws std::runtime_error if the file can't be opened or a required field is missing.
inline NodeConfig parse_config(std::string_view path) {
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(std::string(path));
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load config: " + std::string(e.what()));
    }

    auto require = [&](const YAML::Node& parent, std::string_view key) -> YAML::Node {
        auto node = parent[std::string(key)];
        if (!node) throw std::runtime_error("Config missing: " + std::string(key));
        return node;
    };

    auto parse_address = [&](const YAML::Node& node) -> AddressConfig {
        return {
            .ip   = node["ip"] ? node["ip"].as<std::string>() : "127.0.0.1",
            .port = require(node, "port").as<uint16_t>(),
        };
    };

    NodeConfig cfg {
        .name = require(doc, "name").as<std::string>(),
        .type = require(doc, "type").as<std::string>(),
    };

    for (const auto& ep : require(doc, "endpoints")) {
        cfg.endpoints.push_back({
            .name        = require(ep, "name").as<std::string>(),
            .source      = parse_address(require(ep, "source")),
            .destination = parse_address(require(ep, "destination")),
        });
    }

    if (cfg.endpoints.empty())
        throw std::runtime_error("Config has no endpoints");

    return cfg;
}
