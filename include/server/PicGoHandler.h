#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT

#include <string>
#include "config.h"
#include "http/httplib.h"
#include "db/db_manager.h"

enum class MediaType {
    Photo,
    Video,
    Document,
    Sticker,
    Audio
};

class PicGoHandler {
public:
    PicGoHandler(const Config& config);

    void handleUpload(const httplib::Request& req, httplib::Response& res, const std::string& userId, const std::string& userName, DBManager& dbManager);
    bool parseUrl(const std::string& url, std::string& host, bool& useSSL);
    bool createDirectoryIfNotExists(const std::string& path);
    std::string generateUniqueFilename(const std::string& originalName);
    std::string getFileExtension(const std::string& filename);
    std::string sanitizeFilename(const std::string& filename);
    std::string generateUUID();
    size_t getFileSize(const std::string& filePath);

private:
    const Config& config;

    bool authenticate(const httplib::Request& req);
    bool uploadToTelegram(const std::string& fileContent, const std::string& filename, MediaType mediaType, std::string& telegramFileId);
};
