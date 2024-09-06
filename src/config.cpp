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

bool Config::getUseHttps() const {
    return configData["server"]["use_https"].get<bool>();
}

std::string Config::getSslCertificate() const {
    return configData["server"]["ssl_certificate"].get<std::string>();
}

std::string Config::getSslKey() const {
    return configData["server"]["ssl_key"].get<std::string>();
}

bool Config::getAllowRegistration() const {
    return configData["server"]["allow_registration"].get<bool>();
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

int Config::getCacheMaxSizeMB() const {
	    return configData["cache"]["max_size_mb"].get<int>();
}

int Config::getCacheMaxAgeSeconds() const {
	    return configData["cache"]["max_age_seconds"].get<int>();
}

std::string Config::getWebhookUrl() const {
    return configData["server"]["webhook_url"].get<std::string>();
}

std::string Config::getSecretToken() const {
    return configData["secret_token"].get<std::string>();
}

std::string Config::getOwnerId() const {
    return configData["owner_id"].get<std::string>();
}

std::string Config::getTelegramApiUrl() const {
    return configData["telegram_api_url"].get<std::string>();
}



