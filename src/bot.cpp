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
            std::string chatId = std::to_string(message["chat"]["id"].get<int64_t>());
            Config config("config.json");
            std::string baseUrl = "https://" + config.getHostname() + ":" + std::to_string(config.getPort());

            // 处理图片
            if (message.contains("photo")) {
                std::string fileId = message["photo"].back()["file_id"];
                std::string customUrl = baseUrl + "/images/" + fileId;
                sendMessage(chatId, buildTelegramUrl("图片URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent image URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理文档
            if (message.contains("document")) {
                std::string fileId = message["document"]["file_id"];
                std::string customUrl = baseUrl + "/files/" + fileId;
                sendMessage(chatId, buildTelegramUrl("文件URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent document URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理视频
            if (message.contains("video")) {
                std::string fileId = message["video"]["file_id"];
                std::string customUrl = baseUrl + "/videos/" + fileId;
                sendMessage(chatId, buildTelegramUrl("视频URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent video URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理音频
            if (message.contains("audio")) {
                std::string fileId = message["audio"]["file_id"];
                std::string customUrl = baseUrl + "/audios/" + fileId;
                sendMessage(chatId, buildTelegramUrl("音频URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent audio URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理GIF
            if (message.contains("animation")) {
                std::string fileId = message["animation"]["file_id"];
                std::string customUrl = baseUrl + "/gifs/" + fileId;
                sendMessage(chatId, buildTelegramUrl("GIF URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent GIF URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理贴纸
            if (message.contains("sticker")) {
                std::string fileId = message["sticker"]["file_id"];
                std::string customUrl = baseUrl + "/stickers/" + fileId;
                sendMessage(chatId, buildTelegramUrl("贴纸URL如下: " + customUrl));
                log(LogLevel::INFO,"Sent sticker URL: " + customUrl + " to chat ID: " + chatId);
            }

            // 处理回复中的文件
            if (message.contains("reply_to_message") && message.contains("text")) {
                std::string text = message["text"];
                if (text.find("@YourBotUsername") != std::string::npos) {
                    processUpdate(message["reply_to_message"]);
                }
            }
        }
    } catch (std::exception& e) {
        log(LogLevel::ERROR,"Error processing update: " + std::string(e.what()));
    }
}

std::string Bot::getFileUrl(const std::string& fileId) {
    std::string getFileUrl = buildTelegramUrl("https://api.telegram.org/bot" + apiToken + "/getFile?file_id=" + fileId);
    std::string fileResponse = sendHttpRequest(getFileUrl);

    if (fileResponse.empty()) {
        log(LogLevel::ERROR,"Error retrieving file URL for file ID: " + fileId);
        return {};
    }

    nlohmann::json jsonResponse;
    try {
        jsonResponse = nlohmann::json::parse(fileResponse);
    } catch (nlohmann::json::parse_error& e) {
        log(LogLevel::INFO,"JSON parse error: " + std::string(e.what()));
        return {};
    }

    if (jsonResponse.contains("result") && jsonResponse["result"].contains("file_path")) {
        std::string filePath = jsonResponse["result"]["file_path"];
        return "https://api.telegram.org/file/bot" + apiToken + "/" + filePath;
    } else {
        log(LogLevel::INFO,"Failed to retrieve file path from Telegram API response.");
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
        log(LogLevel::INFO,"Unable to save offset to file.");
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
