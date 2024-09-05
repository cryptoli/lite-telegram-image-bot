#include "bot.h"
#include "db_manager.h"
#include "config.h"
#include "http_client.h"
#include "utils.h"
#include <fstream>

Bot::Bot(const std::string& token) : apiToken(token) {
    initializeOwnerId();  // 初始化时获取Bot的所属者ID
}

// 处理文件并发送文件链接（适用于不同文件类型）
void Bot::handleFileAndSend(const std::string& chatId, const std::string& userId, const std::string& baseUrl, const nlohmann::json& message) {
    if (message.contains("photo")) {
        std::string fileId = message["photo"].back()["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "images", "🖼️", "图片");
    } else if (message.contains("document")) {
        std::string fileId = message["document"]["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "files", "📄", "文件");
    } else if (message.contains("video")) {
        std::string fileId = message["video"]["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "videos", "🎥", "视频");
    } else if (message.contains("audio")) {
        std::string fileId = message["audio"]["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "audios", "🎵", "音频");
    } else if (message.contains("animation")) {
        std::string fileId = message["animation"]["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "gifs", "🎬", "GIF");
    } else if (message.contains("sticker")) {
        std::string fileId = message["sticker"]["file_id"];
        createAndSendFileLink(chatId, userId, fileId, baseUrl, "stickers", "📝", "贴纸");
    } else {
        sendMessage(chatId, "无法处理该文件类型");
    }
}

// 创建并发送文件链接
void Bot::createAndSendFileLink(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName) {
    std::string customUrl = baseUrl + "/" + fileType + "/" + fileId;
    std::string formattedMessage = emoji + " **" + fileType + " URL**:\n" + customUrl;

    // 多线程环境下，独立创建数据库连接
    DBManager dbManager("bot_database.db");

    // 检查是否允许注册
    if (!dbManager.isUserRegistered(userId) && !dbManager.isRegistrationOpen() && !isOwner(userId)) {
        sendMessage(chatId, "注册已关闭，无法收集文件");
        return;
    }

    if (dbManager.addUserIfNotExists(userId, chatId)) {
        dbManager.addFile(userId, fileId, customUrl, fileName);
        sendMessage(chatId, buildTelegramUrl(formattedMessage));
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
            Config config("config.json");

            std::string baseUrl = config.getWebhookUrl();

            // 处理命令
            if (message.contains("text")) {
                std::string text = message["text"];

                if (text == "/collect" && message.contains("reply_to_message")) {
                    const auto& replyMessage = message["reply_to_message"];
                    collectFile(chatId, userId, message["from"].value("username", "unknown"), replyMessage);
                } else if (text == "/remove" && message.contains("reply_to_message")) {
                    removeFile(chatId, userId, message["reply_to_message"]);
                } else if (text == "/ban" && isOwner(userId) && message.contains("reply_to_message")) {
                    banUser(chatId, message["reply_to_message"]);
                } else if (text == "/my") {
                    listMyFiles(chatId, userId);
                } else if (text == "/openregister" && isOwner(userId)) {
                    openRegister(chatId);
                } else if (text == "/closeregister" && isOwner(userId)) {
                    closeRegister(chatId);
                }
            }

            // 处理文件类型
            handleFileAndSend(chatId, userId, baseUrl, message);
        }
    } catch (std::exception& e) {
        log(LogLevel::LOGERROR, "Error processing update: " + std::string(e.what()));
    }
}

// collect命令：收集并保存文件
void Bot::collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage) {
    Config config("config.json");
    std::string baseUrl = config.getWebhookUrl();
    handleFileAndSend(chatId, userId, baseUrl, replyMessage);
}

// remove命令：删除文件
void Bot::removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& replyMessage) {
    if (replyMessage.contains("document")) {
        std::string fileId = replyMessage["document"]["file_id"];
        DBManager dbManager("bot_database.db");
        if (dbManager.removeFile(userId, fileId)) {
            sendMessage(chatId, "文件已删除: " + fileId);
        } else {
            sendMessage(chatId, "删除文件失败或文件不存在");
        }
    } else {
        sendMessage(chatId, "无法删除此类型的文件");
    }
}

// ban命令：封禁用户（仅限拥有者）
void Bot::banUser(const std::string& chatId, const nlohmann::json& replyMessage) {
    std::string userId = std::to_string(replyMessage["from"]["id"].get<int64_t>());
    DBManager dbManager("bot_database.db");
    if (dbManager.isUserRegistered(userId)) {
        if (dbManager.banUser(userId)) {
            sendMessage(chatId, "用户已被封禁: " + userId);
        } else {
            sendMessage(chatId, "封禁用户失败");
        }
    } else {
        sendMessage(chatId, "用户未注册，无法封禁");
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
    std::string sendMessageUrl = "https://api.telegram.org/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + buildTelegramUrl(message);
    sendHttpRequest(sendMessageUrl);
}

void Bot::handleWebhook(const nlohmann::json& webhookRequest) {
    log(LogLevel::INFO, "Received Webhook: " + webhookRequest.dump());
    processUpdate(webhookRequest);
}