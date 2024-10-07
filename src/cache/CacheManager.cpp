#include "CacheManager.h"
#include <iostream>
#include "utils.h"
#include <unordered_set>

CacheManager::CacheManager(size_t maxCacheSize, int cleanupIntervalSeconds)
    : maxCacheSize(maxCacheSize), cleanupIntervalSeconds(cleanupIntervalSeconds), stopThread(false) {
    startCleanupThread();
}

CacheManager::~CacheManager() {
    stopCleanupThread();
}

void CacheManager::addCache(const std::string& key, const std::string& data, int ttlSeconds) {
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cacheMap.size() >= maxCacheSize) {
            cacheMap.erase(cacheMap.begin());
        }
        cacheMap[key] = CacheItem{std::make_unique<std::string>(data), expirationTime};;
    } 
}

bool CacheManager::getCache(const std::string& key, std::string& data) {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cacheMap.find(key);
        if (it != cacheMap.end()) {
            if (now > it->second.expirationTime) {
                cacheMap.erase(it);
                return false;
            }
            data = *(it->second.data);
            return true;
        }
    }
    return false;
}

void CacheManager::addFilePathCache(const std::string& fileId, const std::string& filePath, int ttlSeconds) {
    auto expirationTime = std::chrono::steady_clock::now() + std::chrono::seconds(ttlSeconds);

    {
        std::lock_guard<std::mutex> lock(cacheMutex); 
        if (fileExtensionCache.size() >= maxCacheSize) {
            fileExtensionCache.erase(fileExtensionCache.begin());
        }
        fileExtensionCache[fileId] = CacheItem{std::make_unique<std::string>(filePath), expirationTime};
    }
}

bool CacheManager::getFilePathCache(const std::string& fileId, std::string& filePath) {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = fileExtensionCache.find(fileId);
        if (it != fileExtensionCache.end()) {
            if (now > it->second.expirationTime) {
                fileExtensionCache.erase(it);
                return false;
            }
            filePath = *(it->second.data);
            return true;
        }
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
    auto now = std::chrono::steady_clock::now();  // 先计算当前时间，不需要加锁

    std::lock_guard<std::mutex> lock(cacheMutex);
    auto& info = rateLimitMap[clientIp];

    if (std::chrono::duration_cast<std::chrono::seconds>(now - info.lastRequestTime).count() > 60) {
        info.requestCount = 1;  // 重置请求计数并设为1
        info.lastRequestTime = now;
    } else {
        info.requestCount++;
    }

    if (info.requestCount > maxRequestsPerMinute) {
        return false;  // 超出限流
    }

    return true;
}

// Referer 检查
bool CacheManager::checkReferer(const std::string& referer, const std::unordered_set<std::string>& allowedReferers) {
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
    for (auto it = rateLimitMap.begin(); it != rateLimitMap.end();) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastRequestTime).count() > 60) {
            it = rateLimitMap.erase(it);
        } else {
            ++it;
        }
    }
}

// 清理过期缓存
void CacheManager::cleanupExpiredCache() {
    auto now = std::chrono::steady_clock::now();  // 计算当前时间不需要锁

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // 清理缓存数据
        for (auto it = cacheMap.begin(); it != cacheMap.end();) {
            if (now > it->second.expirationTime) {
                it = cacheMap.erase(it);
            } else {
                ++it;
            }
        }
    } 

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // 清理文件后缀缓存
        for (auto it = fileExtensionCache.begin(); it != fileExtensionCache.end();) {
            if (now > it->second.expirationTime) {
                it = fileExtensionCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    cleanupExpiredRateLimitData(); 
}

// 启动清理线程
void CacheManager::startCleanupThread() {
    cleanupThread = std::thread([this]() {
        while (true) {
            std::unique_lock<std::mutex> lock(cacheMutex);
            // 等待清理间隔时间或 stopThread 为 true 时唤醒
            if (cv.wait_for(lock, std::chrono::seconds(cleanupIntervalSeconds), [this]() { return stopThread; })) {
                break; 
            }
            lock.unlock();
            cleanupExpiredCache();
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
