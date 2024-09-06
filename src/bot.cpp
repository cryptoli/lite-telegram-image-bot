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
    // 定义支持的文件类型及对应的属性
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> fileTypes = {
        {"photo", "images", "🖼️", "图片"},
        {"document", "files", "📄", "文件"},
        {"video", "videos", "🎥", "视频"},
        {"audio", "audios", "🎵", "音频"},
        {"animation", "gifs", "🎬", "GIF"},
        {"sticker", "stickers", "📝", "贴纸"}
    };

    bool fileProcessed = false;  // 用于检查是否处理了至少一个文件

    for (const auto& fileType : fileTypes) {
        const std::string& type = std::get<0>(fileType);
        const std::string& folder = std::get<1>(fileType);
        const std::string& emoji = std::get<2>(fileType);
        const std::string& description = std::get<3>(fileType);

        if (type == "photo" && message.contains("photo")) {
            const auto& photos = message["photo"];
            if (photos.is_array()) {
                // 获取每张图片的最高分辨率
                std::string fileId = photos.back()["file_id"];
                createAndSendFileLink(chatId, userId, fileId, baseUrl, folder, emoji, description);
                fileProcessed = true;  // 标记为已处理文件
            }
        } else if (message.contains(type)) {
            std::string fileId = message[type]["file_id"];
            createAndSendFileLink(chatId, userId, fileId, baseUrl, folder, emoji, description);
            fileProcessed = true;  // 标记为已处理文件
        }
    }

    // 如果没有处理任何文件，提示用户
    if (!fileProcessed) {
        sendMessage(chatId, "请向我发送或转发图片/视频/贴纸/文档/音频/GIF，我会返回对应的url");
    }
}

// 创建并发送文件链接
void Bot::createAndSendFileLink(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName) {
    std::string customUrl = baseUrl + "/" + fileType + "/" + fileId;
    std::string formattedMessage = emoji + " **" + fileName + " URL**:\n" + customUrl;

    // 多线程环境下，独立创建数据库连接
    DBManager dbManager("bot_database.db");

    // 检查是否允许注册
    if (!dbManager.isUserRegistered(userId) && !dbManager.isRegistrationOpen() && !isOwner(userId)) {
        sendMessage(chatId, "注册已关闭，无法收集文件");
        return;
    }

    // 记录文件到数据库并发送消息
    if (dbManager.addUserIfNotExists(userId, chatId)) {
        dbManager.addFile(userId, fileId, customUrl, fileName);
        sendMessage(chatId, formattedMessage);  // 确保在这里发送消息
    } else {
        sendMessage(chatId, "无法收集文件，用户添加失败");
    }
    log(LogLevel::INFO, "Created and sent " + fileType + " URL: " + customUrl + " for chat ID: " + chatId);
}

// 修改的 /my 命令，支持分页并通过按钮切换上下页
void Bot::listMyFiles(const std::string& chatId, const std::string& userId, int page, int pageSize, const std::string& messageId) {
    // 初始化DBManager
    DBManager dbManager("bot_database.db");

    // 获取用户文件总数并计算总页数
    int totalFiles = dbManager.getUserFileCount(userId);
    int totalPages = (totalFiles + pageSize - 1) / pageSize;  // 计算总页数

    if (page > totalPages || page < 1) {
        sendMessage(chatId, "暂无数据");
        return;
    }

    // 获取文件并生成响应
    std::vector<std::pair<std::string, std::string>> files = dbManager.getUserFiles(userId, page, pageSize);
    
    if (files.empty()) {
        sendMessage(chatId, "你还没有收集任何文件。");
    } else {
        std::string response = "你收集的文件 (第 " + std::to_string(page) + " 页，共 " + std::to_string(totalPages) + " 页):\n";
        for (const auto& file : files) {
            response += file.first + ": " + file.second + "\n";
        }

        std::string inlineKeyboard = createPaginationKeyboard(page, totalPages);

        // 如果有messageId，调用editMessageWithKeyboard来更新原消息
        if (!messageId.empty()) {
            editMessageWithKeyboard(chatId, messageId, response, inlineKeyboard);
        } else {
            sendMessageWithKeyboard(chatId, response, inlineKeyboard);
        }
    }
}


// 创建分页键盘
std::string Bot::createPaginationKeyboard(int currentPage, int totalPages) {
    nlohmann::json inlineKeyboard = nlohmann::json::array();

    std::vector<nlohmann::json> row;

    if (currentPage > 1) {
        row.push_back({
            {"text", "⬅️上一页"},
            {"callback_data", "page_" + std::to_string(currentPage - 1)}
        });
    }

    if (currentPage < totalPages) {
        row.push_back({
            {"text", "➡️下一页"},
            {"callback_data", "page_" + std::to_string(currentPage + 1)}
        });
    }

    if (!row.empty()) {
        inlineKeyboard.push_back(row);
    }

    nlohmann::json keyboardMarkup = {
        {"inline_keyboard", inlineKeyboard}
    };

    return keyboardMarkup.dump();  // 转换为字符串用于发送
}

// 发送带有键盘的消息
void Bot::sendMessageWithKeyboard(const std::string& chatId, const std::string& message, const std::string& keyboard) {
    std::string sendMessageUrl = telegramApiUrl + "/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + buildTelegramUrl(message) + "&reply_markup=" + buildTelegramUrl(keyboard);
    sendHttpRequest(sendMessageUrl);
}

// 处理回调查询
void Bot::processCallbackQuery(const nlohmann::json& callbackQuery) {
    if (callbackQuery.contains("data")) {
        std::string callbackData = callbackQuery["data"];
        std::string chatId = std::to_string(callbackQuery["message"]["chat"]["id"].get<int64_t>());
        std::string messageId = std::to_string(callbackQuery["message"]["message_id"].get<int64_t>());
        std::string userId = std::to_string(callbackQuery["from"]["id"].get<int64_t>());

        // 处理页码切换的回调
        if (callbackData.rfind("page_", 0) == 0 && callbackData.length() > 5) {
            int page = std::stoi(callbackData.substr(5));
            listMyFiles(chatId, userId, page, 10, messageId);  // 传递messageId以便更新消息
        }
    }
}

// 在 processUpdate 中处理回调查询
void Bot::processUpdate(const nlohmann::json& update) {
    try {
        log(LogLevel::INFO, "Processing update: " + update.dump());
        if (update.contains("callback_query")) {
            const auto& callbackQuery = update["callback_query"];
            log(LogLevel::INFO, "Processing callback query: " + callbackQuery.dump());
            processCallbackQuery(callbackQuery);  // 处理回调
            return;
        }

        if (update.contains("message")) {
            const auto& message = update["message"];
            std::string chatId = std::to_string(message["chat"]["id"].get<int64_t>());
            std::string userId = std::to_string(message["from"]["id"].get<int64_t>());
            std::string chatType = message["chat"]["type"];  // 获取对话类型（private, group, supergroup 等）
            Config config("config.json");

            std::string baseUrl = config.getWebhookUrl();

            // 处理命令
            if (message.contains("text")) {
                std::string text = message["text"];
                std::string command = text.substr(0, text.find('@'));

                // 群组或超级群组中仅处理命令
                if ((chatType == "group" || chatType == "supergroup") && text[0] != '/') {
                    return;
                }

                // 处理 /collect 命令
                if (command == "/collect" && message.contains("reply_to_message")) {
                    const auto& replyMessage = message["reply_to_message"];
                    collectFile(chatId, userId, message["from"].value("username", "unknown"), replyMessage);
                    return;
                }

                // 处理 /remove 命令
                if (command == "/remove" && message.contains("reply_to_message")) {
                    removeFile(chatId, userId, message["reply_to_message"]);
                    return;
                }

                // 处理 /ban 命令
                if (command == "/ban" && isOwner(userId) && message.contains("reply_to_message")) {
                    banUser(chatId, message["reply_to_message"]);
                    return;
                }

                // 处理 /my 命令
                if (command.rfind("/my", 0) == 0) {
                    int page = 1;
                    if (text.length() > 4) {
                        try {
                            page = std::stoi(text.substr(4));
                        } catch (...) {
                            page = 1;
                        }
                    }
                    listMyFiles(chatId, userId, page);
                    return;
                }

                // 处理 /openregister 和 /closeregister 命令
                if (isOwner(userId)) {
                    if (command == "/openregister") {
                        openRegister(chatId);
                        return;
                    }
                    if (command == "/closeregister") {
                        closeRegister(chatId);
                        return;
                    }
                }
            }

            // 私人聊天中处理文件类型
            if (chatType == "private") {
                handleFileAndSend(chatId, userId, baseUrl, message);
            }
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
    std::vector<std::pair<std::string, std::string>> fileTypes = {
        {"photo", "file_id"},
        {"document", "file_id"},
        {"video", "file_id"},
        {"audio", "file_id"},
        {"animation", "file_id"},
        {"sticker", "file_id"}
    };

    DBManager dbManager("bot_database.db");

    for (const auto& fileType : fileTypes) {
        const std::string& type = fileType.first;
        const std::string& idField = fileType.second;

        if (replyMessage.contains(type)) {
            std::string fileId = replyMessage[type].back()[idField]; // 获取文件ID
            if (dbManager.removeFile(userId, fileId)) {
                sendMessage(chatId, "文件已删除: " + fileId);
            } else {
                sendMessage(chatId, "删除文件失败或文件不存在: " + fileId);
            }
            return;
        }
    }

    // 如果没有匹配的文件类型
    sendMessage(chatId, "无法处理该文件类型");
}

// ban命令：封禁用户（仅限拥有者）
void Bot::banUser(const std::string& chatId, const nlohmann::json& replyMessage) {
    std::string targetUserId = std::to_string(replyMessage["from"]["id"].get<int64_t>());

    // 禁止bot所属者封禁自己
    if (isOwner(targetUserId)) {
        sendMessage(chatId, "无法封禁bot所属者。");
        return;
    }

    DBManager dbManager("bot_database.db");
    if (dbManager.isUserRegistered(targetUserId)) {
        if (dbManager.banUser(targetUserId)) {
            sendMessage(chatId, "用户已被封禁: " + targetUserId);
        } else {
            sendMessage(chatId, "封禁用户失败");
        }
    } else {
        sendMessage(chatId, "用户未注册，无法封禁");
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
    Config config("config.json");
    ownerId = config.getOwnerId();
    telegramApiUrl = config.getTelegramApiUrl();
    log(LogLevel::INFO, "Bot owner ID initialized: " + ownerId);
}

bool Bot::isOwner(const std::string& userId) {
    return userId == ownerId;
}

void Bot::sendMessage(const std::string& chatId, const std::string& message) {
    std::string sendMessageUrl = telegramApiUrl + "/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + buildTelegramUrl(message);
    sendHttpRequest(sendMessageUrl);
}

void Bot::handleWebhook(const nlohmann::json& webhookRequest) {
    log(LogLevel::INFO, "Received Webhook: " + webhookRequest.dump());
    processUpdate(webhookRequest);
}

void Bot::editMessageWithKeyboard(const std::string& chatId, const std::string& messageId, const std::string& message, const std::string& keyboard) {
    std::string editMessageUrl = telegramApiUrl + "/bot" + apiToken + "/editMessageText?chat_id=" + chatId + "&message_id=" + messageId + "&text=" + buildTelegramUrl(message) + "&reply_markup=" + buildTelegramUrl(keyboard);
    sendHttpRequest(editMessageUrl);
}
