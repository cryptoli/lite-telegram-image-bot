#ifndef BOT_H
#define BOT_H

#include <string>
#include <nlohmann/json.hpp>

class Bot {
public:
    Bot(const std::string& token);

    // 处理Telegram更新
    void processUpdate(const nlohmann::json& update);

    // 处理Webhook请求
    void handleWebhook(const nlohmann::json& webhookRequest);

    // 发送消息
    void sendMessage(const std::string& chatId, const std::string& message);

    // 获取文件URL
    std::string getFileUrl(const std::string& fileId);

    // 保存和获取更新的offset
    std::string getOffsetFile();
    void saveOffset(int offset);
    int getSavedOffset();

private:
    std::string apiToken;
    std::string ownerId;

    // 通用文件处理函数
    void handleFile(const std::string& chatId, const std::string& userId, const std::string& fileId, const std::string& baseUrl, const std::string& fileType, const std::string& emoji, const std::string& fileName);

    // 收集文件命令
    void collectFile(const std::string& chatId, const std::string& userId, const std::string& username, const nlohmann::json& replyMessage);

    // 删除文件命令
    void removeFile(const std::string& chatId, const std::string& userId, const nlohmann::json& message);

    // 封禁用户命令
    void banUser(const std::string& chatId, const nlohmann::json& message);

    // 列出用户文件命令
    void listMyFiles(const std::string& chatId, const std::string& userId);

    // 开启和关闭注册
    void openRegister(const std::string& chatId);
    void closeRegister(const std::string& chatId);
    void initializeOwnerId();

    // 判断用户是否为Bot拥有者
    bool isOwner(const std::string& userId);
};

#endif
