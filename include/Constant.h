#ifndef CONSTANT_H
#define CONSTANT_H

#include <iostream>
#include <vector>

static const std::string PHOTO = "photo";
static const std::string FILE_ID = "file_id";
static const std::string ROUTE_PATH = "d";


static const std::string NORMAL_MESSAGE = "**请向我发送或转发图片/视频/贴纸/文档/音频/GIF，我会返回对应的url；将我拉入群聊使用/collet回复其他人发的对话也会返回对应的url。**";
static const std::string CLOSE_REGISTER_MESSAGE = "注册已关闭，无法收集文件";
static const std::string BANNED_MESSAGE = "您已被封禁，无法收集文件";
static const std::string COLLECT_ERROR_MESSAGE = "无法收集文件，用户添加失败";
static const std::string NO_MORE_DATA_MESSAGE = "无数据";
static const std::string NOT_MATCHED_MESSAGE = "无法处理该文件类型";
static const std::string CANNOT_BANNED_OWNER_MESSAGE = "无法封禁bot所属者";

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