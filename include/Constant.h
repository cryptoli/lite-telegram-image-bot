#ifndef CONSTANT_H
#define CONSTANT_H

#include <iostream>
#include <vector>
#include <unordered_map>

// url生成长度
static const int urlLength = 12;
// fileId正则表达式
static const std::string FILE_ID_REGEX = "^[A-Za-z0-9_-]+$";
static const int CURL_TIMEOUT = 30;
static const int CURL_CONNECT_TIMEOUT = 10;
static const long CURL_BUFFERSIZE = 102400L;
static const long CURL_TCP_KEEPALIVE = 1L;
static const long CURL_TCP_KEEPIDLE = 120L;
static const long CURL_TCP_KEEPINTVL = 60L;
static const size_t MAX_GZIP_SIZE = 1048576;

// 默认mime type
static const std::string DEFAULT_MIME_TYPE = "application/octet-stream";
static const std::string MIME_TYPE_IMAGE_JPEG = "image/jpeg";
static const std::string MIME_TYPE_VIDEO_MP4 = "video/mp4";
static const std::string BIN_STRING = "bin";
static const std::string PHOTO = "photo";
static const std::string DOCUMENT = "document";
static const std::string VIDEO = "video";
static const std::string AUDIO = "audio";
static const std::string ANIMATION = "animation";
static const std::string STICKER = "sticker";
static const std::string FILE_ID = "file_id";
static const std::string FILE_SIZE = "file_size";
static const std::string ROUTE_PATH = "d";
static const std::string OK = "ok";
static const std::string FROM = "from";
static const std::string MESSAGE = "message";
static const std::string DATA_STRING = "data";
static const std::string ID_STRING = "id";
static const std::string CHAT_STRING = "chat";
static const std::string MESSAGE_ID = "message_id";
static const std::string TYPE_STRING = "type";
static const std::string CHAT_ID = "chat_id";
static const std::string FROM_CHAT_ID = "from_chat_id";
static const std::string DISABLE_NOTIFICATION = "disable_notification";
static const std::string DOT_STRING = ".";
static const std::string X_REAL_IP = "X-Real-IP";
static const std::string X_FORWARDED_FOR = "X-Forwarded-For";
static const std::string UNKNOWN = "unknown";

static const std::string NORMAL_MESSAGE = "**请向我发送或转发图片/视频/贴纸/文档/音频/GIF，我会返回对应的url；将我拉入群聊使用/collet回复其他人发的对话也会返回对应的url。**";
static const std::string CLOSE_REGISTER_MESSAGE = "注册已关闭";
static const std::string BANNED_MESSAGE = "已被封禁";
static const std::string COLLECT_ERROR_MESSAGE = "无法收集文件，用户添加失败";
static const std::string NO_MORE_DATA_MESSAGE = "无数据";
static const std::string NOT_MATCHED_MESSAGE = "无法处理该文件类型";
static const std::string CANNOT_BANNED_OWNER_MESSAGE = "无法封禁bot所属者";
static const std::string OPEN_REGISTER_MESSAGE = "注册已开启";
static const std::string USER_NOT_REGISTER_MESSAGE = "用户未注册";
static const std::string UNBANNED_MESSAGE = "已解封";

static const std::vector<std::tuple<std::string, std::string, std::string, std::string>> fileTypes = {
    {"photo", "images", "🖼️", "图片"},
    {"document", "files", "📄", "文件"},
    {"video", "videos", "🎥", "视频"},
    {"audio", "audios", "🎵", "音频"},
    {"sticker", "stickers", "📝", "贴纸"}
};

static const std::unordered_map<std::string, std::string> extensionMap = {
        {"jpg", "image"}, {"jpeg", "image"}, {"png", "image"}, {"gif", "image"}, {"bmp", "image"}, 
        {"svg", "image"}, {"webp", "image"}, {"mp4", "video"}, {"mkv", "video"}, {"avi", "video"}, 
        {"mov", "video"}, {"flv", "video"}, {"wmv", "video"}, {"mp3", "audio"}, {"wav", "audio"}, 
        {"aac", "audio"}, {"flac", "audio"}, {"ogg", "audio"}, {"txt", "text"}, {"html", "text"}, 
        {"css", "text"}, {"js", "text"}, {"json", "text"}, {"xml", "text"}, {"pdf", "document"}, 
        {"doc", "document"}, {"docx", "document"}, {"xls", "document"}, {"xlsx", "document"}, 
        {"ppt", "document"}, {"pptx", "document"}, {"zip", "archive"}, {"rar", "archive"}
};
#endif