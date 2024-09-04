#include "bot.h"
#include "db_manager.h"
#include "config.h"
#include "http_client.h"
#include "utils.h"
#include <fstream>

Bot::Bot(const std::string& token) : apiToken(token) {
    initializeOwnerId();  // 初始化时获取Bot的所属者ID
}

// 通用的文件处理函数
void Bot::handleFile(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName) {
    std::string customUrl = baseUrl + "/" + fileType + "/" + fileId;
    std::string formattedMessage = emoji + " **" + fileType + " URL**:\n" + customUrl;

    // 将文件信息存储到数据库
    DBManager dbManager("bot_database.db");
    if (dbManager.addUserIfNotExists(userId, chatId)) {
        dbManager.addFile(userId, fileId, customUrl, fileName);
        sendMessage(chatId, "文件已成功收集，链接为: " + customUrl);
    } else {
        sendMessage(chatId, "无法收集文件，用户添加失败");
    }
    log(LogLevel::INFO, "Sent " + fileType + " URL: " + customUrl + " to chat ID: " + chatId);
}

void Bot::processUpdate(const nlohmann::json& update) {
    try {
        if (update.contains("message")) {
            const auto& message = update["message"];
            std::string chatId = std::to_string(message["chat"]["id"].get<int64_t>());
            std::string userId = std::to_string(message["from"]["id"].get<int64_t>());
            std::string username = message["from"].value("username", "unknown");
            Config config("config.json");

            std::string baseUrl = config.getWebhookUrl();

            // 处理命令
            if (message.contains("text")) {
                std::string text = message["text"];

                if (text == "/collect" && message.contains("reply_to_message")) {
                    const auto& replyMessage = message["reply_to_message"];
                    collectFile(chatId, userId, username, replyMessage);
                } else if (text == "/remove") {
                    removeFile(chatId, userId, message);
                } else if (text == "/ban" && isOwner(userId)) {
                    banUser(chatId, message);
                } else if (text == "/my") {
                    listMyFiles(chatId, userId);
                } else if (text == "/openregister" && isOwner(userId)) {
                    openRegister(chatId);
                } else if (text == "/closeregister" && isOwner(userId)) {
                    closeRegister(chatId);
                }
            }

            // 处理不同类型的文件
            if (message.contains("photo")) {
                std::string fileId = message["photo"].back()["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "images", "🖼️", "图片");
            } else if (message.contains("document")) {
                std::string fileId = message["document"]["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "files", "📄", "文件");
            } else if (message.contains("video")) {
                std::string fileId = message["video"]["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "videos", "🎥", "视频");
            } else if (message.contains("audio")) {
                std::string fileId = message["audio"]["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "audios", "🎵", "音频");
            } else if (message.contains("animation")) {
                std::string fileId = message["animation"]["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "gifs", "🎬", "GIF");
            } else if (message.contains("sticker")) {
                std::string fileId = message["sticker"]["file_id"];
                handleFile(chatId, userId, fileId, baseUrl, "stickers", "📝", "贴纸");
            }
        }
    } catch (std::exception& e) {
        log(LogLevel::LOGERROR, "Error processing update: " + std::string(e.what()));
    }
}

// collect命令：收集并保存文件
void Bot::collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage) {
    DBManager dbManager("bot_database.db");
    Config config("config.json");
    std::string baseUrl = config.getWebhookUrl();

    // 处理图片、文件、贴纸等
    if (replyMessage.contains("photo")) {
        std::string fileId = replyMessage["photo"].back()["file_id"];
        handleFile(chatId, userId, fileId, baseUrl, "images", "🖼️", "图片");
    } else if (replyMessage.contains("document")) {
        std::string fileId = replyMessage["document"]["file_id"];
        handleFile(chatId, userId, fileId, baseUrl, "files", "📄", "文件");
    } else if (replyMessage.contains("sticker")) {
        std::string fileId = replyMessage["sticker"]["file_id"];
        handleFile(chatId, userId, fileId, baseUrl, "stickers", "📝", "贴纸");
    } else {
        sendMessage(chatId, "无法处理该文件类型");
    }
}

// remove命令：删除文件
void Bot::removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& message) {
    DBManager dbManager("bot_database.db");
    std::string fileName = "文件名称";  // 假设通过参数或用户输入获得
    if (dbManager.removeFile(userId, fileName)) {
        sendMessage(chatId, "文件已删除: " + fileName);
    } else {
        sendMessage(chatId, "删除文件失败或文件不存在");
    }
}

// ban命令：封禁用户（仅限拥有者）
void Bot::banUser(const std::string& chatId, const nlohmann::json& message) {
    std::string userId = "被封禁用户的ID";  // 从命令参数中提取
    DBManager dbManager("bot_database.db");
    if (dbManager.banUser(userId)) {
        sendMessage(chatId, "用户已被封禁: " + userId);
    } else {
        sendMessage(chatId, "封禁用户失败");
    }
}

// my命令：列出当前用户的文件
void Bot::listMyFiles(const std::string& chatId, const std::string& userId) {
    DBManager dbManager("bot_database.db");
    std::vector<std::pair<std::string, std::string>> files = dbManager.getUserFiles(userId);
    
    if (files.empty()) {
        sendMessage(chatId, "你还没有收集任何文件");
    } else {
        std::string response = "你收集的文件:\n";
        for (const auto& file : files) {
            response += file.first + ": " + file.second + "\n";
        }
        sendMessage(chatId, response);
    }
}

// openregister命令：开启注册
void Bot::openRegister(const std::string& chatId) {
    DBManager dbManager("bot_database.db");
    dbManager.setRegistrationOpen(true);
    sendMessage(chatId, "注册已开启");
}

// closeregister命令：关闭注册
void Bot::closeRegister(const std::string& chatId) {
    DBManager dbManager("bot_database.db");
    dbManager.setRegistrationOpen(false);
    sendMessage(chatId, "注册已关闭");
}

void Bot::initializeOwnerId() {
    std::string getMeUrl = "https://api.telegram.org/bot" + apiToken + "/getMe";
    std::string response = sendHttpRequest(getMeUrl);

    if (!response.empty()) {
        nlohmann::json jsonResponse = nlohmann::json::parse(response);
        if (jsonResponse.contains("result") && jsonResponse["result"].contains("id")) {
            ownerId = std::to_string(jsonResponse["result"]["id"].get<int64_t>());
            log(LogLevel::INFO, "Bot owner ID initialized: " + ownerId);
        } else {
            log(LogLevel::LOGERROR, "Failed to retrieve owner ID from Telegram");
        }
    } else {
        log(LogLevel::LOGERROR, "No response from Telegram API to retrieve owner ID");
    }
}

bool Bot::isOwner(const std::string& userId) {
    return userId == ownerId;
}

void Bot::sendMessage(const std::string& chatId, const std::string& message) {
    std::string sendMessageUrl = "https://api.telegram.org/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + message;
    sendHttpRequest(sendMessageUrl);
}

void Bot::handleWebhook(const nlohmann::json& webhookRequest) {
    log(LogLevel::INFO, "Received Webhook: " + webhookRequest.dump());
    processUpdate(webhookRequest);
}