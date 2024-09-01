#include "bot.h"
#include "http_client.h"
#include "utils.h"
#include <iostream>
#include <fstream>

Bot::Bot(const std::string& token) : apiToken(token) {}

void Bot::processUpdate(const nlohmann::json& update) {
    try {
        if (update.contains("message")) {
            const auto& message = update["message"];
            if (message.contains("photo")) {
                std::string fileId = message["photo"].back()["file_id"];
                
                std::string chatId = std::to_string(message["chat"]["id"].get<int64_t>());

                std::string fileUrl = getFileUrl(fileId);
                if (!fileUrl.empty()) {
                    sendMessage(chatId, "Here is your image URL: " + fileUrl);
                } else {
                    sendMessage(chatId, "Sorry, I couldn't retrieve the image. Please try again.");
                }
            }

            // 群聊中@机器人的情况
            if (message.contains("reply_to_message") && message.contains("text")) {
                std::string text = message["text"];
                if (text.find("@YourBotUsername") != std::string::npos) {
                    processUpdate(message["reply_to_message"]);
                }
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Error processing update: " << e.what() << std::endl;
    }
}

std::string Bot::getFileUrl(const std::string& fileId) {
    std::string getFileUrl = "https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId;
    std::string fileResponse = sendHttpRequest(getFileUrl);

    if (fileResponse.empty()) {
        return {};
    }

    nlohmann::json jsonResponse;
    try {
        jsonResponse = nlohmann::json::parse(fileResponse);
    } catch (nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return {};
    }

    if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
        std::string filePath = jsonResponse["result"]["file_path"];
        return "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;
    } else {
        std::cerr << "Failed to retrieve file path from Telegram API response." << std::endl;
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
        std::cerr << "Unable to save offset to file." << std::endl;
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
