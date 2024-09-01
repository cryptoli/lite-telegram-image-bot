#include "config.h"
#include <fstream>
#include <iostream>

Config::Config(const std::string& filePath) {
    std::ifstream configFile(filePath);
    if (configFile.is_open()) {
        configFile >> configData;
        configFile.close();
    } else {
        throw std::runtime_error("Unable to open config file");
    }
}

std::string Config::getHostname() const {
    return configData["server"]["hostname"].get<std::string>();
}

int Config::getPort() const {
    return configData["server"]["port"].get<int>();
}

std::string Config::getApiToken() const {
    return configData["api_token"].get<std::string>();
}

std::map<std::string, std::string> Config::getMimeTypes() const {
    std::map<std::string, std::string> mimeTypes;
    for (auto& item : configData["mime_types"].items()) {
        mimeTypes[item.key()] = item.value();
    }
    return mimeTypes;
}