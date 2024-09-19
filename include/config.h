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
    bool getUseHttps() const;
    std::string getSslCertificate() const;
    std::string getSslKey() const;
    bool getAllowRegistration() const;
    std::string getApiToken() const;
    std::map<std::string, std::string> getMimeTypes() const;
    int getCacheMaxSizeMB() const;
    int getCacheMaxAgeSeconds() const;
    std::string getWebhookUrl() const;
    std::string getSecretToken() const;
    std::string getOwnerId() const;
    std::string getTelegramApiUrl() const;
    bool enableReferers() const;
    std::vector<std::string> getAllowedReferers() const;
    int getRateLimitRequestsPerMinute() const;
    std::string getTelegramChannelId() const;


private:
    nlohmann::json configData;
};

#endif

