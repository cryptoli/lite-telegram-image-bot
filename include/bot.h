#ifndef BOT_H
#define BOT_H

#include <string>
#include <nlohmann/json.hpp>
#include <map>
#include "db_manager.h"
#include "config.h"

class Bot {
public:
    Bot(const std::string& token, DBManager& dbManager);
    void forwardMessageToChannel(const nlohmann::json& message);
    void handleFileAndSend(const std::string& chatId, const std::string& userId, const std::string& baseUrl, const nlohmann::json& message, const std::string& username);
    void createAndSendFileLink(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName, const std::string& username);
    void processUpdate(const nlohmann::json& update);
    void processCallbackQuery(const nlohmann::json& callbackQuery);
    void listMyFiles(const std::string& chatId, const std::string& userId, int page, int pageSize = 10, const std::string& messageId = "");
    void listRemovableFiles(const std::string& chatId, const std::string& userId, int page, int pageSize = 10, const std::string& messageId = "");
    void listUsersForBan(const std::string& chatId, int page, int pageSize = 10, const std::string& messageId = "");
    std::string createPaginationKeyboard(int currentPage, int totalPages);
    void sendMessage(const std::string& chatId, const std::string& message);
    void sendMessageWithKeyboard(const std::string& chatId, const std::string& message, const std::string& keyboard);
    void collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage);
    void removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& replyMessage);
    void banUser(const std::string& chatId, const nlohmann::json& replyMessage);
    void banUserById(const std::string& chatId, const std::string& targetUserId);
    void toggleBanUser(const std::string& chatId, const std::string& targetUserId, const std::string& messageId);
    void openRegister(const std::string& chatId);
    void closeRegister(const std::string& chatId);
    void handleWebhook(const nlohmann::json& webhookRequest);
    void initializeOwnerId();
    bool isOwner(const std::string& userId);
    void editMessageWithKeyboard(const std::string& chatId, const std::string& messageId, const std::string& message, const std::string& keyboard);

private:
    std::string apiToken;
    std::string telegramApiUrl;
    std::string ownerId;
    DBManager& dbManager;
    Config config;
};

#endif