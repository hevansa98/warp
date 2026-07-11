#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <yaml-cpp/yaml.h>

struct NodeConfig {
    std::string name;
    std::string ip;
    std::string type;
    uint16_t    porta{0};
    uint16_t    portadest{0};
    uint16_t    portb{0};
    uint16_t    portbdest{0};
};

// Loads a YAML config file and returns a NodeConfig.
// Throws std::runtime_error if the file can't be opened or a field is missing.
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

    return {
        .name       = require(doc, "name").as<std::string>(),
        .ip         = require(doc, "ip").as<std::string>(),
        .type       = require(doc, "type").as<std::string>(),
        .porta      = require(require(doc, "sidea"), "port").as<uint16_t>(),
        .portadest  = require(require(doc, "sidea"), "destination").as<uint16_t>(),
        .portb      = require(require(doc, "sideb"), "port").as<uint16_t>(),
        .portbdest  = require(require(doc, "sideb"), "destination").as<uint16_t>(),
    };
}
