#include "config.h"
#include <fstream>
#include <iostream>
#include <cstdlib>

Config::Config(const std::string& filePath) {
    std::ifstream configFile(filePath);
    if (configFile.is_open()) {
        try {
            configFile >> configData;  // 解析JSON数据
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error("Error parsing config file: " + std::string(e.what()));
        }
        configFile.close();
    } else {
        throw std::runtime_error("Unable to open config file");
    }
}

std::string Config::getHostname() const {
    const char* envHostname = std::getenv("HOST_NAME");
    if (envHostname != nullptr) {
        return std::string(envHostname);
    }
    return configData["server"]["hostname"].get<std::string>();
}

int Config::getPort() const {
    const char* envPort = std::getenv("PORT");
    if (envPort != nullptr) {
        try {
            return std::stoi(envPort);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Invalid PORT value in environment variable: " << envPort << std::endl;
        } catch (const std::out_of_range& e) {
            std::cerr << "PORT value out of range in environment variable: " << envPort << std::endl;
        }
    }
    return configData["server"]["port"].get<int>();
}

bool Config::getUseHttps() const {
    const char* envUseHttps = std::getenv("USE_HTTPS");
    if (envUseHttps != nullptr) {
        return std::string(envUseHttps) == "true";
    }
    return configData["server"]["use_https"].get<bool>();
}

std::string Config::getSslCertificate() const {
    const char* envSslCert = std::getenv("SSL_CERTIFICATE");
    if (envSslCert != nullptr) {
        return std::string(envSslCert);
    }
    return configData["server"]["ssl_certificate"].get<std::string>();
}

std::string Config::getSslKey() const {
    const char* envSslKey = std::getenv("SSL_KEY");
    if (envSslKey != nullptr) {
        return std::string(envSslKey);
    }
    return configData["server"]["ssl_key"].get<std::string>();
}

bool Config::getAllowRegistration() const {
    const char* envAllowReg = std::getenv("ALLOW_REGISTRATION");
    if (envAllowReg != nullptr) {
        return std::string(envAllowReg) == "true";
    }
    return configData["server"]["allow_registration"].get<bool>();
}

std::string Config::getApiToken() const {
    const char* envApiToken = std::getenv("API_TOKEN");
    if (envApiToken != nullptr) {
        return std::string(envApiToken);
    }
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
    const char* envCacheSize = std::getenv("CACHE_MAX_SIZE_MB");
    if (envCacheSize != nullptr) {
        return std::stoi(envCacheSize);
    }
    return configData["cache"]["max_size_mb"].get<int>();
}

int Config::getCacheMaxAgeSeconds() const {
    const char* envCacheAge = std::getenv("CACHE_MAX_AGE_SECONDS");
    if (envCacheAge != nullptr) {
        return std::stoi(envCacheAge);
    }
    return configData["cache"]["max_age_seconds"].get<int>();
}

std::string Config::getWebhookUrl() const {
    const char* envWebhookUrl = std::getenv("WEBHOOK_URL");
    if (envWebhookUrl != nullptr) {
        return std::string(envWebhookUrl);
    }
    return configData["server"]["webhook_url"].get<std::string>();
}

std::string Config::getSecretToken() const {
    const char* envSecretToken = std::getenv("SECRET_TOKEN");
    if (envSecretToken != nullptr) {
        return std::string(envSecretToken);
    }
    return configData["secret_token"].get<std::string>();
}

std::string Config::getOwnerId() const {
    const char* envOwnerId = std::getenv("OWNER_ID");
    if (envOwnerId != nullptr) {
        return std::string(envOwnerId);
    }
    return configData["owner_id"].get<std::string>();
}

std::string Config::getTelegramApiUrl() const {
    const char* envTelegramApiUrl = std::getenv("TELEGRAM_API_URL");
    if (envTelegramApiUrl != nullptr) {
        return std::string(envTelegramApiUrl);
    }
    return configData["telegram_api_url"].get<std::string>();
}

// Security configurations
bool Config::enableReferers() const {
    return configData["security"]["enable_referers"].get<bool>();
}

std::vector<std::string> Config::getAllowedReferers() const {
    std::vector<std::string> allowedReferers;
    for (const auto& referer : configData["security"]["allowed_referers"]) {
        allowedReferers.push_back(referer.get<std::string>());
    }
    return allowedReferers;
}

int Config::getRateLimitRequestsPerMinute() const {
    return configData["security"]["rate_limit"]["requests_per_minute"].get<int>();
}

std::string Config::getTelegramChannelId() const {
    const char* channelId = std::getenv("TELEGRAM_CHANNEL_ID");
    if (channelId != nullptr) {
        return std::string(channelId);
    }
    return configData["channel_id"].get<std::string>();
}
