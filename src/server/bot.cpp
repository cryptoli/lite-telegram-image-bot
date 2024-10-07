#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "bot.h"
#include "db/db_manager.h"
#include "config.h"
#include "http/http_client.h"
#include "utils.h"
#include <fstream>
#include <iostream>
#include "http/httplib.h"
#include "constant.h"

Bot::Bot(const std::string& token, DBManager& dbManager) : apiToken(token), dbManager(dbManager), config("config.json") {
    initializeOwnerId();  // 初始化时获取Bot的所属者ID
}

// 处理文件并发送文件链接（适用于不同文件类型）
void Bot::handleFileAndSend(const std::string& chatId, const std::string& userId, const std::string& baseUrl, const nlohmann::json& message, const std::string& username) {
    bool fileProcessed = false;

    for (const auto& fileType : fileTypes) {
        const std::string& type = std::get<0>(fileType);
        const std::string& emoji = std::get<2>(fileType);
        const std::string& description = std::get<3>(fileType);

        if (type == PHOTO && message.contains(PHOTO)) {
            const auto& photos = message[PHOTO];
            if (photos.is_array()) {
                // 获取每张图片的最高分辨率
                std::string fileId = photos.back()[FILE_ID];
                int fileSize = photos.back()[FILE_SIZE];
                createAndSendFileLink(chatId, userId, fileId, baseUrl, ROUTE_PATH, emoji, description, username, fileSize);
                fileProcessed = true;
            }
        } else if (message.contains(type)) {
            std::string fileId = message[type][FILE_ID];
            int fileSize = message[type][FILE_SIZE];
            createAndSendFileLink(chatId, userId, fileId, baseUrl, ROUTE_PATH, emoji, description, username, fileSize);
            fileProcessed = true;
        }
    }

    if (!fileProcessed) {
        sendMessage(chatId, NORMAL_MESSAGE);
    }
}

// 创建并发送文件链接
void Bot::createAndSendFileLink(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName, const std::string& username, int fileSize) {
    std::string shortId = generateShortLink(fileId);
    std::ostringstream customUrlStream;
    customUrlStream << baseUrl << "/" << fileType << "/" << shortId;
    std::string customUrl = customUrlStream.str();
    std::ostringstream oss;
    oss << emoji << " **" << fileName << " URL**:\n"
        << "文件大小: " << std::to_string(static_cast<double>(fileSize) / (1024 * 1024)) << " MB\n"
        << "直链：" << customUrl << "\n"
        << "点击复制链接文本：\n`" << customUrl << "`\n"
        << "点击复制Markdown格式代码：\n`![](" << customUrl << ")`\n"
        << "点击复制html格式代码：\n`<img src=\"" << customUrl << "\">`\n"
        << "点击复制ubb格式代码：\n`[img]" << customUrl << "[/img]`";
    std::string formattedMessage = oss.str();

    // 检查是否允许注册
    if (!dbManager.isUserRegistered(userId) && !dbManager.isRegistrationOpen() && !isOwner(userId)) {
        sendMessage(chatId, CLOSE_REGISTER_MESSAGE);
        return;
    }
    // 检查是否被封禁
    if(!isOwner(userId) && dbManager.isUserBanned(userId)) {
        sendMessage(chatId, BANNED_MESSAGE);
        return;
    }
    // 记录文件到数据库并发送消息
    if (dbManager.addUserIfNotExists(userId, username)) {
        dbManager.addFile(userId, fileId, customUrl, fileName, shortId, customUrl, "");
        sendMessage(chatId, formattedMessage);

        log(LogLevel::INFO, "Created and sent ", fileType, " URL: ", customUrl, " for chat ID: ", chatId, ", for username: ", username);
    } else {
        sendMessage(chatId, COLLECT_ERROR_MESSAGE);
    }
}

// 修改的 /my 命令，支持分页并通过按钮切换上下页
void Bot::listMyFiles(const std::string& chatId, const std::string& userId, int page, int pageSize, const std::string& messageId) {
    int totalFiles = dbManager.getUserFileCount(userId);
    int totalPages = (totalFiles + pageSize - 1) / pageSize;

    if (page > totalPages || page < 1) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
        return;
    }

    std::vector<std::tuple<std::string, std::string, std::string>> files = dbManager.getUserFiles(userId, page, pageSize);
    
    if (files.empty()) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
    } else {
        std::string response = "你收集的文件 (第 " + std::to_string(page) + " 页，共 " + std::to_string(totalPages) + " 页):\n";
        for (const auto& file : files) {
            response += std::get<0>(file) + ": " + std::get<1>(file) + "\n";
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

void Bot::listRemovableFiles(const std::string& chatId, const std::string& userId, int page, int pageSize, const std::string& messageId) {
    int totalFiles = dbManager.getUserFileCount(userId);
    int totalPages = (totalFiles + pageSize - 1) / pageSize;

    if (page > totalPages || page < 1) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
        return;
    }

    std::vector<std::tuple<std::string, std::string, std::string>> files = dbManager.getUserFiles(userId, page, pageSize);
    
    if (files.empty()) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
    } else {
        std::string response = "请选择你要删除的文件 (第 " + std::to_string(page) + " 页，共 " + std::to_string(totalPages) + " 页):\n";
        nlohmann::json inlineKeyboard = nlohmann::json::array();

        for (const auto& file : files) {
            std::string fileId = std::get<2>(file);
            std::string fileUrl = std::get<1>(file);
            
            // 每个文件生成一行按钮
            inlineKeyboard.push_back({
                {{"text", fileUrl}, {"callback_data", "delete_" + fileId}}
            });
        }

        if (page > 1) {
            inlineKeyboard.push_back({{{"text", "⬅️上一页"}, {"callback_data", "remove_page_" + std::to_string(page - 1)}}});
        }

        if (page < totalPages) {
            inlineKeyboard.push_back({{{"text", "➡️下一页"}, {"callback_data", "remove_page_" + std::to_string(page + 1)}}});
        }

        nlohmann::json keyboardMarkup = {{"inline_keyboard", inlineKeyboard}};

        if (!messageId.empty()) {
            editMessageWithKeyboard(chatId, messageId, response, keyboardMarkup.dump());
        } else {
            sendMessageWithKeyboard(chatId, response, keyboardMarkup.dump());
        }
    }
}

void Bot::listUsersForBan(const std::string& chatId, int page, int pageSize, const std::string& messageId) {
    int totalUsers = dbManager.getTotalUserCount();
    int totalPages = (totalUsers + pageSize - 1) / pageSize;

    if (page > totalPages || page < 1) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
        return;
    }

    std::vector<std::tuple<std::string, std::string, bool>> users = dbManager.getUsersForBan(page, pageSize);
    
    if (users.empty()) {
        sendMessage(chatId, NO_MORE_DATA_MESSAGE);
    } else {
        std::string response = "请选择要封禁/解封的用户 (第 " + std::to_string(page) + " 页，共 " + std::to_string(totalPages) + " 页):\n";
        nlohmann::json inlineKeyboard = nlohmann::json::array();

        for (const auto& user : users) {
            std::string userId = std::get<0>(user);
            std::string username = std::get<1>(user);
            bool isBanned = std::get<2>(user);

            std::string buttonText = username + (isBanned ? " [已封禁]" : " [有效]");
            std::string callbackData = "toggleban_" + userId;

            inlineKeyboard.push_back({
                {{"text", buttonText}, {"callback_data", callbackData}}
            });
        }
        if (page > 1) {
            inlineKeyboard.push_back({{{"text", "⬅️上一页"}, {"callback_data", "ban_page_" + std::to_string(page - 1)}}});
        }
        if (page < totalPages) {
            inlineKeyboard.push_back({{{"text", "➡️下一页"}, {"callback_data", "ban_page_" + std::to_string(page + 1)}}});
        }

        nlohmann::json keyboardMarkup = {{"inline_keyboard", inlineKeyboard}};

        if (!messageId.empty()) {
            editMessageWithKeyboard(chatId, messageId, response, keyboardMarkup.dump());
        } else {
            sendMessageWithKeyboard(chatId, response, keyboardMarkup.dump());
        }
    }
}

// 发送带有键盘的消息
void Bot::sendMessageWithKeyboard(const std::string& chatId, const std::string& message, const std::string& keyboard) {
    std::string sendMessageUrl = telegramApiUrl + "/bot" + apiToken + "/sendMessage?chat_id=" + chatId + "&text=" + buildTelegramUrl(message) + "&reply_markup=" + buildTelegramUrl(keyboard);
    sendHttpRequest(sendMessageUrl);
}

void Bot::processCallbackQuery(const nlohmann::json& callbackQuery) {
    if (callbackQuery.contains(DATA_STRING)) {
        std::string callbackData = callbackQuery[DATA_STRING];
        std::string chatId = std::to_string(callbackQuery[MESSAGE][CHAT_STRING][ID_STRING].get<int64_t>());
        std::string messageId = std::to_string(callbackQuery[MESSAGE][MESSAGE_ID].get<int64_t>());
        std::string userId = std::to_string(callbackQuery[FROM][ID_STRING].get<int64_t>());

        if (callbackData.rfind("page_", 0) == 0 && callbackData.length() > 5) {
            int page = std::stoi(callbackData.substr(5));
            listMyFiles(chatId, userId, page, 10, messageId);
        }
        // 处理分页回调
        else if (callbackData.rfind("remove_page_", 0) == 0) {
            int page = std::stoi(callbackData.substr(12));
            log(LogLevel::INFO, chatId+ ",", userId, ",list removable fils.");
            listRemovableFiles(chatId, userId, page, 10, messageId);  // 更新消息
        }
        // 处理删除文件回调
        else if (callbackData.rfind("delete_", 0) == 0) {
            std::string fileId = callbackData.substr(7);
            log(LogLevel::INFO, userId, " delete file: ", fileId, ", callbackData: ", callbackQuery.dump());
            if (dbManager.removeFile(userId, fileId)) {
                sendMessage(chatId, "文件已删除: " + fileId);
            } else {
                sendMessage(chatId, "删除文件失败或文件不存在: " + fileId);
            }

            // 刷新文件列表
            listRemovableFiles(chatId, userId, 1, 10, messageId);
        }
        else if (callbackData.rfind("ban_page_", 0) == 0) {
            int page = std::stoi(callbackData.substr(9));
            listUsersForBan(chatId, page, 10, messageId);
            return;
        }
        else if (callbackData.rfind("ban_", 0) == 0) {
            std::string targetUserId = callbackData.substr(4);
            banUserById(chatId, targetUserId);
            return;
        }
        else if (callbackData.rfind("toggleban_", 0) == 0) {
            std::string targetUserId = callbackData.substr(10);
            toggleBanUser(chatId, targetUserId, messageId);
            return;
        }
    }
}

// 在 processUpdate 中处理回调查询
void Bot::processUpdate(const nlohmann::json& update, ThreadPool& pool) {
    try {
        log(LogLevel::INFO, "Processing update: ", update.dump());
        if (update.contains("callback_query")) {
            const auto& callbackQuery = update["callback_query"];
            log(LogLevel::INFO, "Processing callback query: ", callbackQuery.dump());
            processCallbackQuery(callbackQuery);  // 处理回调
            return;
        }

        if (update.contains(MESSAGE)) {
            const auto& message = update[MESSAGE];
            std::string chatId = std::to_string(message[CHAT_STRING][ID_STRING].get<int64_t>());
            std::string userId = std::to_string(message[FROM][ID_STRING].get<int64_t>());
            std::string chatType = message[CHAT_STRING][TYPE_STRING];  // 获取对话类型（private, group, supergroup 等）

            std::string baseUrl = config.getWebhookUrl();
            
            // 处理命令
            if (message.contains("text")) {
                std::string text = message["text"];
                std::string command = text.substr(0, text.find('@'));

                if ((chatType == "group" || chatType == "supergroup" || chatType == "channel") && text[0] != '/') {
                    return;
                }
                if (command == "/collect" && message.contains("reply_to_message")) {
                    pool.enqueue([=, &message]() {
                        forwardMessageToChannel(message);
                    });
                    const auto& replyMessage = message["reply_to_message"];
                    collectFile(chatId, userId, message["from"].value("username", "unknown"), replyMessage);
                    return;
                }
                if (command == "/remove") {
                    int page = 1;
                    if (text.length() > 7) {
                        try {
                            page = std::stoi(text.substr(7));
                        } catch (...) {
                            page = 1;
                        }
                    }
                    listRemovableFiles(chatId, userId, page, 10);
                    return;
                }
                if (command == "/ban" && isOwner(userId)) {
                    // banUser(chatId, message["reply_to_message"]);
                    listUsersForBan(chatId, 1, 10, "");
                    return;
                }
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
            if (chatType == "private") {
                pool.enqueue([=, &message]() {
                    forwardMessageToChannel(message);
                });
                handleFileAndSend(chatId, userId, baseUrl, message, message[FROM].value("username", "unknown"));
            }
        }
    } catch (std::exception& e) {
        log(LogLevel::LOGERROR, "Error processing update: ", std::string(e.what()));
    }
}

void Bot::forwardMessageToChannel(const nlohmann::json& message) {
    try {
        std::string fromChatId = std::to_string(message[CHAT_STRING][ID_STRING].get<int64_t>());
        int64_t messageId = message[MESSAGE_ID].get<int64_t>();

        std::string channelId = config.getTelegramChannelId();

        nlohmann::json requestBody = {
            {CHAT_ID, channelId},
            {FROM_CHAT_ID, fromChatId},
            {MESSAGE_ID, messageId},
            {DISABLE_NOTIFICATION, true}
        };

        httplib::Client cli(config.getTelegramApiUrl());
        cli.set_read_timeout(10, 0);
    
        auto res = cli.Post(("/bot" + config.getApiToken() + "/forwardMessage").c_str(), requestBody.dump(), "application/json");
        if (res && res->status == 200) {
            log(LogLevel::INFO, "Message forwarded to channel successfully.");
        } else {
            log(LogLevel::LOGERROR, "Failed to forward message to channel.");
            if (res) {
                log(LogLevel::LOGERROR, "Status code: ", std::to_string(res->status));
                log(LogLevel::LOGERROR, "Response: ", res->body);
            } else {
                log(LogLevel::LOGERROR, "No response from Telegram API.");
            }
        }
    } catch (std::exception& e) {
        log(LogLevel::LOGERROR, "Error processing forwardMessageToChannel: ", std::string(e.what()));
    } catch (...) {
        log(LogLevel::LOGERROR, "Error processing forwardMessageToChannel");
    }
}

// collect命令：收集并保存文件
void Bot::collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage) {
    std::string baseUrl = config.getWebhookUrl();
    handleFileAndSend(chatId, userId, baseUrl, replyMessage, username);
}

// remove命令：删除文件
void Bot::removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& replyMessage) {
    std::vector<std::pair<std::string, std::string>> fileTypes = {
        {PHOTO, FILE_ID},
        {DOCUMENT, FILE_ID},
        {VIDEO, FILE_ID},
        {AUDIO, FILE_ID},
        {ANIMATION, FILE_ID},
        {STICKER, FILE_ID}
    };

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
    sendMessage(chatId, NOT_MATCHED_MESSAGE);
}

// ban命令：封禁用户（仅限拥有者）
void Bot::banUser(const std::string& chatId, const nlohmann::json& replyMessage) {
    std::string targetUserId = std::to_string(replyMessage[FROM][ID_STRING].get<int64_t>());

    // 禁止bot所属者封禁自己
    if (isOwner(targetUserId)) {
        sendMessage(chatId, CANNOT_BANNED_OWNER_MESSAGE);
        return;
    }

    // DBManager dbManager("bot_database.db");
    if (dbManager.isUserRegistered(targetUserId)) {
        if (dbManager.banUser(targetUserId)) {
            sendMessage(chatId, "用户已被封禁: " + targetUserId);
        } else {
            sendMessage(chatId, "封禁用户失败");
        }
    } else {
        sendMessage(chatId, USER_NOT_REGISTER_MESSAGE);
    }
}

void Bot::banUserById(const std::string& chatId, const std::string& targetUserId) {
    if (isOwner(targetUserId)) {
        sendMessage(chatId, CANNOT_BANNED_OWNER_MESSAGE);
        return;
    }

    // Ban the user if they exist
    if (dbManager.isUserRegistered(targetUserId)) {
        if (dbManager.banUser(targetUserId)) {
            sendMessage(chatId, "用户已被封禁: " + targetUserId);
        } else {
            sendMessage(chatId, "封禁用户失败");
        }
    } else {
        sendMessage(chatId, USER_NOT_REGISTER_MESSAGE);
    }
}

void Bot::toggleBanUser(const std::string& chatId, const std::string& targetUserId, const std::string& messageId) {
    if (isOwner(targetUserId)) {
        sendMessage(chatId, CANNOT_BANNED_OWNER_MESSAGE);
        return;
    }

    if (dbManager.isUserRegistered(targetUserId)) {
        bool isBanned = dbManager.isUserBanned(targetUserId);

        if (isBanned) {
            dbManager.unbanUser(targetUserId);
            sendMessage(chatId, targetUserId + UNBANNED_MESSAGE);
        } else {
            dbManager.banUser(targetUserId);
            sendMessage(chatId, targetUserId + BANNED_MESSAGE);
        }

        listUsersForBan(chatId, 1, 10, messageId);
    } else {
        sendMessage(chatId, USER_NOT_REGISTER_MESSAGE);
    }
}

// openregister命令：开启注册
void Bot::openRegister(const std::string& chatId) {
    dbManager.setRegistrationOpen(true);
    sendMessage(chatId, OPEN_REGISTER_MESSAGE);
}

// closeregister命令：关闭注册
void Bot::closeRegister(const std::string& chatId) {
    dbManager.setRegistrationOpen(false);
    sendMessage(chatId, CLOSE_REGISTER_MESSAGE);
}

void Bot::initializeOwnerId() {
    ownerId = config.getOwnerId();
    telegramApiUrl = config.getTelegramApiUrl();
    log(LogLevel::INFO, "Bot owner ID initialized: ", ownerId);
}

bool Bot::isOwner(const std::string& userId) {
    log(LogLevel::INFO, "Bot ownerId: ", ownerId, ", userId: ", userId, ", ", (userId == ownerId ? "true" : "false"));
    return userId == ownerId;
}

void Bot::sendMessage(const std::string& chatId, const std::string& message) {
    std::ostringstream sendMessageUrlStream;
    sendMessageUrlStream << telegramApiUrl << "/bot" << apiToken 
                         << "/sendMessage?chat_id=" << chatId 
                         << "&parse_mode=MarkdownV2&text=" << buildTelegramUrl(escapeTelegramUrl(message));

    sendHttpRequest(sendMessageUrlStream.str());
}

void Bot::handleWebhook(const nlohmann::json& webhookRequest, ThreadPool& pool) {
    log(LogLevel::INFO, "Received Webhook: ", webhookRequest.dump());
    processUpdate(webhookRequest, pool);
}

void Bot::editMessageWithKeyboard(const std::string& chatId, const std::string& messageId, const std::string& message, const std::string& keyboard) {
    std::ostringstream editMessageUrlStream;
    editMessageUrlStream << telegramApiUrl << "/bot" << apiToken 
                         << "/editMessageText?chat_id=" << chatId 
                         << "&message_id=" << messageId 
                         << "&text=" << buildTelegramUrl(message) 
                         << "&reply_markup=" << buildTelegramUrl(keyboard);

    sendHttpRequest(editMessageUrlStream.str());
}
