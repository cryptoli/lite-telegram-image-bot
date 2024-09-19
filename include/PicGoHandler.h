#pragma once

#include <string>
#include "config.h"
#include "httplib.h"
#include "db_manager.h"

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

    void handleUpload(const httplib::Request& req, httplib::Response& res, const std::string userId, const std::string userName, DBManager& dbManager);

private:
    const Config& config;

    bool authenticate(const httplib::Request& req);
    bool uploadToTelegram(const std::string& filePath, MediaType mediaType, std::string& telegramFileId);
};
