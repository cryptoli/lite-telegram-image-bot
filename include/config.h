#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <nlohmann/json.hpp>
#include <map>

class Config {
public:
    Config(const std::string& filePath);
    std::string getHostname() const;
    int getPort() const;
    std::map<std::string, std::string> getMimeTypes() const;

private:
    nlohmann::json configData;
};

#endif
