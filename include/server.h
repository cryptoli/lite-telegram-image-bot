#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <functional>
#include "httplib.h"
#include "config.h"
#include "CacheManager.h"
#include "thread_pool.h"
#include "StatisticsManager.h"
#include "image_cache_manager.h"
#include "bot.h"

// 获取客户端真实 IP 地址
std::string getClientIp(const httplib::Request& req);

// 统一处理媒体请求
void handleMediaRequest(const httplib::Request& req, httplib::Response& res, const Config& config, CacheManager& cacheManager,
                        const std::function<void(const httplib::Request&, httplib::Response&)>& handler,
                        StatisticsManager& statisticsManager, ThreadPool& pool);

// 处理请求统计信息
void handleRequestStatistics(const httplib::Request& req, httplib::Response& res, const std::string& requestPath,
                             StatisticsManager& statisticsManager, ThreadPool& pool, int responseTime, int requestLatency);

// 确定文件类型
std::string determineFileType(const std::string& requestPath);

// 加载模板文件
std::string loadTemplate(const std::string& filepath);

// 启动服务器
void startServer(const Config& config, ImageCacheManager& cacheManager, ThreadPool& pool, Bot& bot, CacheManager& rateLimiter, DBManager& dbManager);

// 处理图片请求
void handleImageRequest(const httplib::Request& req, httplib::Response& res, const std::string& apiToken,
                        const std::map<std::string, std::string>& mimeTypes, ImageCacheManager& cacheManager,
                        CacheManager& memoryCache, const std::string& telegramApiUrl, const Config& config, DBManager& dbManager);

#endif 
