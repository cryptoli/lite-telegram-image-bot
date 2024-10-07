#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <regex>
#include "http/httplib.h"
#include "server/PicGoHandler.h"
#include "utils.h"
#include "constant.h"

using json = nlohmann::json;

PicGoHandler::PicGoHandler(const Config& config)
    : config(config) {}

// 处理 PicGo 的上传请求
void PicGoHandler::handleUpload(const httplib::Request& req, httplib::Response& res,
                                const std::string& userId, const std::string& userName,
                                DBManager& dbManager) {
    if (req.method != "POST") {
        res.status = 405;
        res.set_content(R"({"error":"Method Not Allowed"})", "application/json");
        return;
    }

    auto file = req.get_file_value("image");
    if (file.content.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"Bad Request: No image uploaded"})", "application/json");
        return;
    }

    std::string filename = sanitizeFilename(file.filename);
    if (filename.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"Invalid file name"})", "application/json");
        return;
    }

    // 上传Telegram
    std::string telegramFileId;
    if (!uploadToTelegram(file.content, filename, MediaType::Photo, telegramFileId)) {
        res.status = 500;
        res.set_content(R"({"error":"Internal Server Error: Failed to upload to Telegram"})", "application/json");
        return;
    }

    std::string shortId = generateShortLink(telegramFileId);
    std::string customUrl = config.getWebhookUrl() + "/d/" + shortId;

    json result;
    result["success"] = true;
    result["file_id"] = telegramFileId;
    result["url"] = customUrl;

    if (!dbManager.addUserIfNotExists(userId, userName)) {
        log(LogLevel::LOGERROR, "Error adding user to database.");
    }

    if (!dbManager.addFile(userId, telegramFileId, customUrl, filename, shortId, customUrl, "")) {
        log(LogLevel::LOGERROR, "Error adding file to database.");
    }

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}

// 上传图片到 Telegram
bool PicGoHandler::uploadToTelegram(const std::string& fileContent, const std::string& filename,
                                    MediaType mediaType, std::string& telegramFileId) {
    try {
        log(LogLevel::INFO, "Starting uploadToTelegram for file: ", filename);

        std::string apiMethod;
        std::string fileField;

        // 根据媒体类型确定 API 方法和文件字段名
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
                log(LogLevel::LOGERROR, "Unknown media type for file: ", filename);
                return false;
        }

        // 初始化 HTTP 客户端
        std::string apiUrl = config.getTelegramApiUrl();
        httplib::Client cli(apiUrl);

        if (!cli.is_valid()) {
            log(LogLevel::LOGERROR, "HTTP client initialization failed for apiUrl: ", apiUrl);
            return false;
        }

        // 设置超时
        cli.set_read_timeout(60, 0);  // 60 秒

        // 准备表单数据
        httplib::MultipartFormDataItems items = {
            {"chat_id", config.getTelegramChannelId(), "", ""},
            {fileField, fileContent, filename, "application/octet-stream"}
        };

        // 构建请求路径
        std::string apiPath = "/bot" + config.getApiToken() + "/" + apiMethod;

        // 发送 POST 请求
        auto res = cli.Post(apiPath.c_str(), items);

        if (!res) {
            log(LogLevel::LOGERROR, "No response from Telegram API. Possible connection issue.");
            return false;
        }

        log(LogLevel::INFO, "Received response from Telegram API, status code: ",
                            std::to_string(res->status));

        if (res->status != 200) {
            log(LogLevel::LOGERROR, "Unexpected status code from Telegram API: ",
                                    std::to_string(res->status));
            log(LogLevel::LOGERROR, "Response body: ", res->body);
            return false;
        }

        auto responseJson = json::parse(res->body);

        if (responseJson[OK].template get<bool>()) {
            if (mediaType == MediaType::Photo) {
                telegramFileId = responseJson["result"]["photo"].back()["file_id"].template get<std::string>();
            } else if (mediaType == MediaType::Document) {
                telegramFileId = responseJson["result"]["document"]["file_id"].template get<std::string>();
            } else if (mediaType == MediaType::Video) {
                telegramFileId = responseJson["result"]["video"]["file_id"].template get<std::string>();
            } else if (mediaType == MediaType::Audio) {
                telegramFileId = responseJson["result"]["audio"]["file_id"].template get<std::string>();
            } else if (mediaType == MediaType::Sticker) {
                telegramFileId = responseJson["result"]["sticker"]["file_id"].template get<std::string>();
            } else {
                log(LogLevel::LOGERROR, "Unsupported media type for extracting file_id.");
                return false;
            }

            log(LogLevel::INFO, "File uploaded successfully, Telegram file ID: ", telegramFileId);
            return true;
        } else {
            log(LogLevel::LOGERROR, "Telegram API returned an error: ", res->body);
        }

    } catch (const std::exception& e) {
        log(LogLevel::LOGERROR, "Exception occurred: ", std::string(e.what()));
        return false;
    }

    return false;
}

bool PicGoHandler::createDirectoryIfNotExists(const std::string& path) {
#ifdef _WIN32
    // Windows 实现
    DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        // 目录不存在，尝试创建
        if (!CreateDirectoryA(path.c_str(), NULL)) {
            log(LogLevel::LOGERROR, "Failed to create directory: ", path,
                                    ", error code: ", std::to_string(GetLastError()));
            return false;
        }
    } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        // 路径存在，但不是目录
        log(LogLevel::LOGERROR, "Path exists but is not a directory: ", path);
        return false;
    }
    return true;
#else
    // Linux/Unix 实现
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        // 目录不存在，尝试创建
        if (mkdir(path.c_str(), 0755) != 0) {
            log(LogLevel::LOGERROR, "Failed to create directory: ", path,
                                    ", error: ", strerror(errno));
            return false;
        }
    } else if (!S_ISDIR(info.st_mode)) {
        // 路径存在，但不是目录
        log(LogLevel::LOGERROR, "Path exists but is not a directory: ", path);
        return false;
    }
    return true;
#endif
}

std::string PicGoHandler::generateUniqueFilename(const std::string& originalName) {
    std::string extension = getFileExtension(originalName);
    std::string uniqueName = generateUUID();
    return uniqueName + extension;
}

std::string PicGoHandler::getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        return filename.substr(pos);
    }
    return "";
}

std::string PicGoHandler::sanitizeFilename(const std::string& filename) {
    std::string sanitized = filename;
    sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '/'), sanitized.end());
    sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\\'), sanitized.end());
    return sanitized;
}

std::string PicGoHandler::generateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    int i;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4"; // 第三组的第一个字符固定为 '4'
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen); // 第四组的第一个字符为 '8'、'9'、'A' 或 'B'
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    return ss.str();
}

