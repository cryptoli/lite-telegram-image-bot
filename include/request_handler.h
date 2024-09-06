#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <string>
#include <map>
#include <httplib.h>
#include "image_cache_manager.h"

// 获取文件的 MIME 类型
std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes, const std::string& defaultMimeType);

// 获取文件的扩展名
std::string getFileExtension(const std::string& filePath);

// 流式传输回调函数
size_t streamWriteCallback(void* ptr, size_t size, size_t nmemb, httplib::Response* res);

// 处理视频和文档的直接流式传输（不缓存）
void handleStreamRequest(const httplib::Request& req, httplib::Response& res, const std::string& fileDownloadUrl, const std::string& mimeType);

// 处理图片、非视频和非文档文件的缓存请求
void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager, const std::string& telegramApiUrl);

#endif