#ifndef BOT_H
#define BOT_H

#include <string>
#include <nlohmann/json.hpp>

class Bot {
public:
    Bot(const std::string& token);
    void processUpdate(const nlohmann::json& update);
    std::string getFileUrl(const std::string& fileId);
    void sendMessage(const std::string& chatId, const std::string& message);

private:
    std::string apiToken;
    std::string getOffsetFile();
    void saveOffset(int offset);
    int getSavedOffset();
};

#endif