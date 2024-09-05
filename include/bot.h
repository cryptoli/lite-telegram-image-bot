#ifndef BOT_H
#define BOT_H

#include <string>
#include <nlohmann/json.hpp>
#include <vector>

class Bot {
public:
    // 构造函数，初始化 Bot 并获取 Telegram Token
    Bot(const std::string& token);

    // 处理来自 Telegram 的更新
    void processUpdate(const nlohmann::json& update);

    // 处理Webhook请求
    void handleWebhook(const nlohmann::json& webhookRequest);

    // 发送消息
    void sendMessage(const std::string& chatId, const std::string& message);

    // 初始化Bot所属者ID
    void initializeOwnerId();

    // 判断用户是否为Bot的拥有者
    bool isOwner(const std::string& userId);

private:
    std::string apiToken;  // Telegram API Token
    std::string ownerId;   // Bot 所属者的 ID

    // 处理收集文件命令
    void collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage);

    // 处理删除文件命令
    void removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& replyMessage);

    // 处理封禁用户命令
    void banUser(const std::string& chatId, const nlohmann::json& replyMessage);

    // 分页列出当前用户的文件
    void listMyFiles(const std::string& chatId, const std::string& userId, int page = 1, int pageSize = 5);

    // 开启注册
    void openRegister(const std::string& chatId);

    // 关闭注册
    void closeRegister(const std::string& chatId);

    // 处理并发送文件链接
    void handleFileAndSend(const std::string& chatId, const std::string& userId, const std::string& baseUrl, const nlohmann::json& message);

    // 创建并发送文件链接
    void createAndSendFileLink(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName);
};

#endif