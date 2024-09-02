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

            std::string baseUrl = config.getUseHttps() ? "https://" : "http://";  // Ê†πÊçÆ use_https ÈÄâÊã©ÂçèËÆÆ
            baseUrl += config.getHostname();
            int port = config.getPort();
            if (port != 80 && port != 443) {
                baseUrl += ":" + std::to_string(port);
            }

            // Â§ÑÁêÜÂõæÁâá
            if (message.contains("photo")) {
                std::string fileId = message["photo"].back()["file_id"];
                std::string customUrl = baseUrl + "/images/" + fileId;
                std::string formattedMessage = "Ì†ΩÌ∂ºÔ∏è **ÂõæÁâá URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent image URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜÊñáÊ°£
            if (message.contains("document")) {
                std::string fileId = message["document"]["file_id"];
                std::string customUrl = baseUrl + "/files/" + fileId;
                std::string formattedMessage = "Ì†ΩÌ≥Ñ **Êñá‰ª∂ URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent document URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜËßÜÈ¢ë
            if (message.contains("video")) {
                std::string fileId = message["video"]["file_id"];
                std::string customUrl = baseUrl + "/videos/" + fileId;
                std::string formattedMessage = "Ì†ºÌæ• **ËßÜÈ¢ë URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent video URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜÈü≥È¢ë
            if (message.contains("audio")) {
                std::string fileId = message["audio"]["file_id"];
                std::string customUrl = baseUrl + "/audios/" + fileId;
                std::string formattedMessage = "Ì†ºÌæµ **Èü≥È¢ë URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent audio URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜGIF
            if (message.contains("animation")) {
                std::string fileId = message["animation"]["file_id"];
                std::string customUrl = baseUrl + "/gifs/" + fileId;
                std::string formattedMessage = "Ì†ºÌæ¨ **GIF URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent GIF URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜË¥¥Á∫∏
            if (message.contains("sticker")) {
                std::string fileId = message["sticker"]["file_id"];
                std::string customUrl = baseUrl + "/stickers/" + fileId;
                std::string formattedMessage = "Ì†ΩÌ≥ù **Ë¥¥Á∫∏ URL**:\n" + customUrl;
                sendMessage(chatId, buildTelegramUrl(formattedMessage));
                log(LogLevel::INFO,"Sent sticker URL: " + customUrl + " to chat ID: " + chatId);
            }

            // Â§ÑÁêÜÂõûÂ§ç‰∏≠ÁöÑÊñá‰ª∂
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

void Bot::handleWebhook(const nlohmann::json& webhookRequest) {
    log(LogLevel::INFO,"Received Webhook: " + webhookRequest.dump());
    processUpdate(webhookRequest);
}

