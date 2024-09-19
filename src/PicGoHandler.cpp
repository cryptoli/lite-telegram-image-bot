#include "PicGoHandler.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "utils.h"

using json = nlohmann::json;

PicGoHandler::PicGoHandler(const Config& config)
    : config(config) {}

// 处理 PicGo 的上传请求
void PicGoHandler::handleUpload(const httplib::Request& req, httplib::Response& res, const std::string userId, const std::string userName, DBManager& dbManager) {
    // 验证请求方法
    if (req.method != "POST") {
        res.status = 405;
        res.set_content("Method Not Allowed", "text/plain");
        return;
    }

    // 检查请求中是否包含文件
    auto file = req.get_file_value("image");
    if (file.content.empty()) {
        res.status = 400;
        res.set_content("Bad Request: No image uploaded", "text/plain");
        return;
    }

    // 将上传的文件保存到临时目录
    std::string tempFilePath = "cache/" + file.filename;
    std::ofstream ofs(tempFilePath, std::ios::binary);
    ofs << file.content;
    ofs.close();

    // 上传到 Telegram
    std::string telegramFileId;
    if (!uploadToTelegram(tempFilePath, MediaType::Photo, telegramFileId)) {
        res.status = 500;
        res.set_content("Internal Server Error: Failed to upload to Telegram", "text/plain");
        return;
    }

    // 删除临时文件
    std::remove(tempFilePath.c_str());

    std::string shortId = generateShortLink(telegramFileId);
    std::string customUrl = config.getTelegramApiUrl() + "/d/" + shortId;

    // 返回上传结果
    json result;
    result["success"] = true;
    result["file_id"] = telegramFileId;
    result["url"] = customUrl;

    // 记录文件到数据库并发送消息
    if (dbManager.addUserIfNotExists(userId, userName)) {
        dbManager.addFile(userId, telegramFileId, customUrl, file.filename, shortId, customUrl, "");
    } else {
        log(LogLevel::LOGERROR, "Error addFile! ");
    }

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}

// 上传图片到 Telegram
bool PicGoHandler::uploadToTelegram(const std::string& filePath, MediaType mediaType, std::string& telegramFileId) {
    // 根据媒体类型确定 API 方法和文件字段名
    std::string apiMethod;
    std::string fileField;

    switch (mediaType) {
        case MediaType::Photo:
            apiMethod = "sendPhoto";
            fileField = "photo";
            break;
        case MediaType::Video:
            apiMethod = "sendVideo";
            fileField = "video";
            break;
        case MediaType::Document:
            apiMethod = "sendDocument";
            fileField = "document";
            break;
        case MediaType::Sticker:
            apiMethod = "sendSticker";
            fileField = "sticker";
            break;
        case MediaType::Audio:
            apiMethod = "sendAudio";
            fileField = "audio";
            break;
        default:
            log(LogLevel::LOGERROR, "Unknown media type");
            return false;
    }

    // 构造 Telegram API 请求 URL
    std::string apiUrl = config.getTelegramApiUrl() + "/bot" + config.getApiToken() + "/" + apiMethod;

    // 使用 httplib 的客户端发送请求
    httplib::Client cli(config.getTelegramApiUrl().c_str());

    // 准备表单数据
    httplib::MultipartFormDataItems items = {
        {"chat_id", config.getTelegramChannelId(), "", ""},
        {fileField, "", filePath, "application/octet-stream"}
    };

    // 发送 POST 请求
    auto res = cli.Post(("/bot" + config.getApiToken() + "/" + apiMethod).c_str(), items);

    if (res && res->status == 200) {
        // 解析返回的 JSON 数据
        try {
            auto responseJson = json::parse(res->body);
            if (responseJson["ok"].get<bool>()) {
                telegramFileId = responseJson["result"]["message_id"].get<std::string>();
                return true;
            }
        } catch (const std::exception& e) {
            log(LogLevel::LOGERROR, "Error parsing Telegram API response: " + std::string(e.what()));
        }
    }

    return false;
}
