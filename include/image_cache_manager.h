#ifndef IMAGE_CACHE_MANAGER_H
#define IMAGE_CACHE_MANAGER_H

#include "utils.h"
#include <string>
#include <unordered_map>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <algorithm>
#include <limits.h>

class ImageCacheManager {
public:
    ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds)
        : maxDiskUsageBytes(maxDiskUsageMB * 1024 * 1024), maxCacheAgeSeconds(maxCacheAgeSeconds) {

        // 将相对路径转换为绝对路径
        char absolutePath[PATH_MAX];
        if (realpath(cacheDir.c_str(), absolutePath) != nullptr) {
            this->cacheDir = std::string(absolutePath);
        } else {
            this->cacheDir = cacheDir;
            log(LogLevel::ERROR, "Failed to resolve cache directory to absolute path: " + cacheDir);
        }

        if (!directoryExists(this->cacheDir)) {
            mkdir(this->cacheDir.c_str(), 0777);
            log(LogLevel::INFO, "Created cache directory: " + this->cacheDir);
        }

        cleanerThread = std::thread(&ImageCacheManager::cleanUpOldFiles, this);
    }

    ~ImageCacheManager() {
        stopCleaner = true;
        if (cleanerThread.joinable()) {
            cleanerThread.join();
        }
        log(LogLevel::INFO, "Cache manager cleaned up and exited.");
    }

    void cacheImage(const std::string& fileId, const std::string& imageData, const std::string& extension) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId, extension);

        std::ofstream file(filePath.c_str(), std::ios::binary);  // 确保以二进制模式写入
        if (file.is_open()) {
            file.write(imageData.c_str(), imageData.size());
            file.close();
            lastAccessTimes[fileId] = std::chrono::system_clock::now();
            log(LogLevel::INFO, "Cached image: " + fileId + " at " + filePath);
        } else {
            log(LogLevel::ERROR, "Failed to open file for caching: " + filePath);
        }
    }

    std::string getCachedImage(const std::string& fileId, const std::string& extension) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId, extension);

        if (fileExists(filePath)) {
            std::ifstream file(filePath.c_str(), std::ios::binary);  // 确保以二进制模式读取
            if (file.is_open()) {
                std::string imageData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                lastAccessTimes[fileId] = std::chrono::system_clock::now();
                log(LogLevel::INFO, "Cache hit: " + fileId + " from " + filePath);
                return imageData;
            } else {
                log(LogLevel::ERROR, "Failed to open cached file: " + filePath);
            }
        } else {
            log(LogLevel::WARNING, "Cache miss for file ID: " + fileId);
        }

        return ""; // Cache miss
    }

private:
    std::string cacheDir;
    size_t maxDiskUsageBytes;
    int maxCacheAgeSeconds;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> lastAccessTimes;
    std::mutex cacheMutex;
    std::thread cleanerThread;
    bool stopCleaner = false;

    std::string getCacheFilePath(const std::string& fileId, const std::string& extension) const {
        return cacheDir + "/" + fileId + extension;
    }

    size_t getCacheSize() const {
        size_t totalSize = 0;
        DIR* dirp = opendir(cacheDir.c_str());
        struct dirent* entry;
        while ((entry = readdir(dirp)) != nullptr) {
            std::string filePath = cacheDir + "/" + entry->d_name;
            struct stat filestat;
            if (stat(filePath.c_str(), &filestat) == 0 && S_ISREG(filestat.st_mode)) {
                totalSize += filestat.st_size;
            }
        }
        closedir(dirp);
        return totalSize;
    }

    bool directoryExists(const std::string& path) {
        struct stat info;
        if (stat(path.c_str(), &info) != 0) {
            return false;
        } else if (info.st_mode & S_IFDIR) {
            return true;
        } else {
            return false;
        }
    }

    bool fileExists(const std::string& path) {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }

    void cleanUpOldFiles() {
        while (!stopCleaner) {
            std::this_thread::sleep_for(std::chrono::seconds(600)); // 每600秒检查一次

            std::lock_guard<std::mutex> lock(cacheMutex);

            auto now = std::chrono::system_clock::now();
            size_t currentCacheSize = getCacheSize();

            for (auto it = lastAccessTimes.begin(); it != lastAccessTimes.end();) {
                std::string filePath = getCacheFilePath(it->first, ".cache");

                if (fileExists(filePath)) {
                    auto fileAge = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();

                    // 删除长时间未访问的文件
                    if (fileAge > maxCacheAgeSeconds) {
                        remove(filePath.c_str());
                        it = lastAccessTimes.erase(it);
                        log(LogLevel::INFO, "Removed old cached image: " + filePath);
                    } else {
                        ++it;
                    }
                } else {
                    it = lastAccessTimes.erase(it);
                }
            }

            // 检查磁盘使用情况并清理缓存
            while (currentCacheSize > maxDiskUsageBytes) {
                auto oldest = std::min_element(lastAccessTimes.begin(), lastAccessTimes.end(),
                    [](const std::pair<const std::string, std::chrono::system_clock::time_point>& lhs,
                       const std::pair<const std::string, std::chrono::system_clock::time_point>& rhs) {
                        return lhs.second < rhs.second;
                    });

                if (oldest != lastAccessTimes.end()) {
                    std::string oldestFilePath = getCacheFilePath(oldest->first, ".cache");
                    if (fileExists(oldestFilePath)) {
                        currentCacheSize -= getFileSize(oldestFilePath);
                        remove(oldestFilePath.c_str());
                        lastAccessTimes.erase(oldest);
                        log(LogLevel::INFO, "Removed image due to disk space limit: " + oldestFilePath);
                    }
                } else {
                    break;
                }
            }
        }
    }

    size_t getFileSize(const std::string& path) {
        struct stat filestat;
        if (stat(path.c_str(), &filestat) == 0) {
            return filestat.st_size;
        }
        return 0;
    }
};

#endif
