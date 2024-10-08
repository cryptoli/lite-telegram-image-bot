#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <string>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <thread>
#include <unordered_set>

// 缓存项结构
struct CacheItem {
    std::unique_ptr<std::string> data;
    std::chrono::steady_clock::time_point expirationTime;
};

struct RateLimitInfo {
    std::chrono::steady_clock::time_point lastRequestTime;
    int requestCount;
};

// 缓存管理类
class CacheManager {
public:
    CacheManager(size_t maxCacheSize, int cleanupIntervalSeconds);
    ~CacheManager();

    // 添加缓存
    void addCache(const std::string& key, const std::string& data, int ttlSeconds);

    // 获取缓存
    bool getCache(const std::string& key, std::string& data);
    void addFilePathCache(const std::string& fileId, const std::string& filePath, int ttlSeconds);
    bool getFilePathCache(const std::string& fileId, std::string& filePath);

    // 删除缓存
    void deleteCache(const std::string& key);

    // 限流检查
    bool checkRateLimit(const std::string& clientIp, int maxRequestsPerMinute);

    // Referer 检查
    bool checkReferer(const std::string& referer, const std::unordered_set<std::string>& allowedReferers);

    // 启动清理线程
    void startCleanupThread();

    // 停止清理线程
    void stopCleanupThread();

private:
    void cleanupExpiredCache();  // 清理过期缓存

    std::unordered_map<std::string, CacheItem> cacheMap;
    std::unordered_map<std::string, int> requestCounts;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> requestTimestamps;
    std::unordered_map<std::string, CacheItem> fileExtensionCache;
    std::unordered_map<std::string, RateLimitInfo> rateLimitMap;

    std::mutex cacheMutex;
    size_t maxCacheSize;
    void cleanupExpiredRateLimitData();
    int cleanupIntervalSeconds;
    bool stopThread;
    std::thread cleanupThread;
    std::condition_variable cv;
};

#endif
