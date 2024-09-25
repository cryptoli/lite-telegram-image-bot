#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <string>
#include <map>
#include <httplib.h>
#include "image_cache_manager.h"
#include "db_manager.h"
#include "config.h"
#include "CacheManager.h"
#include "thread_pool.h"
#include "CacheManager.h"
#include "StatisticsManager.h"

// 获取文件的 MIME 类型
std::string getMimeType(const std::string& filePath, const std::map<std::string, std::string>& mimeTypes, const std::string& defaultMimeType);

// 获取文件的扩展名
std::string getFileExtension(const std::string& filePath);

// 流式传输回调函数
size_t streamWriteCallback(void* ptr, size_t size, size_t nmemb, httplib::Response* res);

// 处理视频和文档的直接流式传输（不缓存）
void handleStreamRequest(const httplib::Request& req, httplib::Response& res, const std::string& fileDownloadUrl, const std::string& mimeType);

// 处理图片、非视频和非文档文件的缓存请求
void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken, const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager,CacheManager& memoryCache, const std::string& telegramApiUrl, const Config& config, DBManager& dbManager);

std::string getBaseUrl(const std::string& url);
void setHttpResponse(httplib::Response& res, const std::string& fileData, const std::string& mimeType, const httplib::Request& req);
void unifiedInterceptor(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& rateLimiter,
                       std::function<void(const httplib::Request&, httplib::Response&)> handler,
                       StatisticsManager& statisticsManager, ThreadPool& pool);
std::string getClientIp(const httplib::Request& req);
void handleMediaRequestWithTiming(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& cacheManager,
                                  const std::function<void(const httplib::Request&, httplib::Response&)>& handler,
                                  StatisticsManager& statisticsManager, ThreadPool& pool, int requestLatency);
std::string determineFileType(const std::string& requestPath);
void handleRequestStatistics(const httplib::Request& req, httplib::Response& res, const std::string& requestPath,
                             StatisticsManager& statisticsManager, ThreadPool& pool, int responseTime, int requestLatency);

#endif