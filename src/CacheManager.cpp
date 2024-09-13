#include "CacheManager.h"
#include <iostream>
#include "utils.h"

CacheManager::CacheManager(size_t maxCacheSize, int cleanupIntervalSeconds)
    : maxCacheSize(maxCacheSize), cleanupIntervalSeconds(cleanupIntervalSeconds), stopThread(false) {
    startCleanupThread();
}

CacheManager::~CacheManager() {
    stopCleanupThread();
}

// 添加缓存
void CacheManager::addCache(const std::string& key, const std::string& data, int ttlSeconds) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
    cacheMap[key] = {data, expirationTime};
}

// 获取缓存
bool CacheManager::getCache(const std::string& key, std::string& data) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cacheMap.find(key);
    if (it != cacheMap.end()) {
        if (std::chrono::steady_clock::now() > it->second.expirationTime) {
            cacheMap.erase(it);
            return false;
        }
        data = it->second.data;
        return true;
    }
    return false;
}

void CacheManager::addFilePathCache(const std::string& fileId, const std::string& filePath, int ttlSeconds) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);
    fileExtensionCache[fileId] = {filePath, expirationTime};  // 存储文件后缀缓存
}

bool CacheManager::getFilePathCache(const std::string& fileId, std::string& filePath) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = fileExtensionCache.find(fileId);
    if (it != fileExtensionCache.end()) {
        if (std::chrono::steady_clock::now() > it->second.expirationTime) {
            fileExtensionCache.erase(it);
            return false;
        }
        filePath = it->second.data;
        return true;
    }
    return false;
}

// 删除缓存
void CacheManager::deleteCache(const std::string& key) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    cacheMap.erase(key);
}

// 限流检查
bool CacheManager::checkRateLimit(const std::string& clientIp, int maxRequestsPerMinute) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto now = std::chrono::steady_clock::now();
    auto& lastRequestTime = requestTimestamps[clientIp];
    auto& requestCount = requestCounts[clientIp];

    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRequestTime).count() > 60) {
        requestCount = 0;  // 重置请求计数
        lastRequestTime = now;
    }

    requestCount++;
    if (requestCount > maxRequestsPerMinute) {
        return false;  // 超出限流
    }

    return true;
}

// Referer 检查
bool CacheManager::checkReferer(const std::string& referer, const std::vector<std::string>& allowedReferers) {
    for (const auto& allowed : allowedReferers) {
        if (referer.find(allowed) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 定期清理过期的限流数据
void CacheManager::cleanupExpiredRateLimitData() {
    auto now = std::chrono::steady_clock::now();
    
    // 遍历并删除超过 60 秒未请求的 IP 地址
    for (auto it = requestTimestamps.begin(); it != requestTimestamps.end();) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 60) {
            requestCounts.erase(it->first);  // 同时删除 requestCounts 中对应的条目
            it = requestTimestamps.erase(it);  // 删除 requestTimestamps 中的条目
        } else {
            ++it;
        }
    }
}

// 清理过期缓存
void CacheManager::cleanupExpiredCache() {
    auto now = std::chrono::steady_clock::now();
    
    // 清理缓存数据
    for (auto it = cacheMap.begin(); it != cacheMap.end();) {
        if (now > it->second.expirationTime) {
            it = cacheMap.erase(it);
        } else {
            ++it;
        }
    }

    // 清理文件后缀缓存
    for (auto it = fileExtensionCache.begin(); it != fileExtensionCache.end();) {
        if (now > it->second.expirationTime) {
            it = fileExtensionCache.erase(it);
        } else {
            ++it;
        }
    }

    // 清理过期的限流数据
    cleanupExpiredRateLimitData();
}

// 启动清理线程
void CacheManager::startCleanupThread() {
    cleanupThread = std::thread([this]() {
        while (true) {
            std::unique_lock<std::mutex> lock(cacheMutex);
            // 等待清理间隔时间或 stopThread 为 true 时唤醒
            if (cv.wait_for(lock, std::chrono::seconds(cleanupIntervalSeconds), [this]() { return stopThread; })) {
                break; // 如果 stopThread 为 true，退出线程
            }
            lock.unlock();  // 解锁，允许其他线程使用 cacheMutex
            cleanupExpiredCache();  // 执行清理操作
        }
    });
}

// 停止清理线程
void CacheManager::stopCleanupThread() {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        stopThread = true;
    }
    cv.notify_all();  // 通知清理线程停止
    if (cleanupThread.joinable()) {
        cleanupThread.join();  // 等待清理线程结束
    }
}
