#include "bot.h"
#include "config.h"
#include "http_client.h"
#include "utils.h"
#include <fstream>

Bot::Bot(const std::string& token) : apiToken(token) {}

void Bot::processUpdate(const nlohmann::json& update) {
    try {
        if (update.contains("message")) {
            const auto& message = update["message"];
            if (message.contains("photo")) {
                std::string fileId = message["photo"].back()["file_id"];
                
                // 生成自定义链接
                Config config("config.json");
                std::string customUrl = "http://" + config.getHostname() + ":" + std::to_string(config.getPort()) + "/images/" + fileId;
                std::string chatId = std::to_string(message["chat"]["id"].get<int64_t>());

                sendMessage(chatId, "Here is your image URL: " + buildTelegramUrl(customUrl));
                log("Sent image URL: " + customUrl + " to chat ID: " + chatId);
            }

            if (message.contains("reply_to_message") && message.contains("text")) {
                std::string text = message["text"];
                if (text.find("@YourBotUsername") != std::string::npos) {
                    processUpdate(message["reply_to_message"]);
                }
            }
        }
    } catch (std::exception& e) {
        log("Error processing update: " + std::string(e.what()));
    }
}

std::string Bot::getFileUrl(const std::string& fileId) {
    std::string getFileUrl = buildTelegramUrl("https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId);
    std::string fileResponse = sendHttpRequest(getFileUrl);

    if (fileResponse.empty()) {
        log("Error retrieving file URL for file ID: " + fileId);
        return {};
    }

    nlohmann::json jsonResponse;
    try {
        jsonResponse = nlohmann::json::parse(fileResponse);
    } catch (nlohmann::json::parse_error& e) {
        log("JSON parse error: " + std::string(e.what()));
        return {};
    }

    if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
        std::string filePath = jsonResponse["result"]["file_path"];
        return "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;
    } else {
        log("Failed to retrieve file path from Telegram API response.");
        return {};
    }
}

void Bot::sendMessage(const std::string& chatId, const std::string& message) {
    std::string sendMessageUrl = "https://api.telegram.org/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + message;
    sendHttpRequest(sendMessageUrl);
}

std::string Bot::getOffsetFile() {
    return "offset.txt";
}

void Bot::saveOffset(int offset) {
    std::ofstream offsetFile(getOffsetFile());
    if (offsetFile.is_open()) {
        offsetFile << offset;
        offsetFile.close();
    } else {
        log("Unable to save offset to file.");
    }
}

int Bot::getSavedOffset() {
    std::ifstream offsetFile(getOffsetFile());
    int offset = 0;
    if (offsetFile.is_open()) {
        offsetFile >> offset;
        offsetFile.close();
    }
    return offset;
}
