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

class ImageCacheManager {
public:
    ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds)
        : cacheDir(cacheDir), maxDiskUsageBytes(maxDiskUsageMB * 1024 * 1024), maxCacheAgeSeconds(maxCacheAgeSeconds) {
        if (!directoryExists(cacheDir)) {
            mkdir(cacheDir.c_str(), 0777);
        }
        cleanerThread = std::thread(&ImageCacheManager::cleanUpOldFiles, this);
    }

    ~ImageCacheManager() {
        stopCleaner = true;
        if (cleanerThread.joinable()) {
            cleanerThread.join();
        }
    }

    void cacheImage(const std::string& fileId, const std::string& imageData) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId);

        std::ofstream file(filePath.c_str(), std::ios::binary);
        if (file.is_open()) {
            file.write(imageData.c_str(), imageData.size());
            file.close();
            lastAccessTimes[fileId] = std::chrono::system_clock::now();
            std::cout << "Cached image: " << fileId << std::endl;
        }
    }

    std::string getCachedImage(const std::string& fileId) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId);

        if (fileExists(filePath)) {
            lastAccessTimes[fileId] = std::chrono::system_clock::now();

            std::ifstream file(filePath.c_str(), std::ios::binary);
            std::string imageData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return imageData;
        } else {
            log(LogLevel::WARNING,"Cache miss.");
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

    std::string getCacheFilePath(const std::string& fileId) const {
        return cacheDir + "/" + fileId + ".cache";
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
                std::string filePath = getCacheFilePath(it->first);

                if (fileExists(filePath)) {
                    auto fileAge = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();

                    // 删除长时间未访问的文件
                    if (fileAge > maxCacheAgeSeconds) {
                        remove(filePath.c_str());
                        it = lastAccessTimes.erase(it);
                        std::cout << "Removed old cached image: " << filePath << std::endl;
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
                    std::string oldestFilePath = getCacheFilePath(oldest->first);
                    if (fileExists(oldestFilePath)) {
                        currentCacheSize -= getFileSize(oldestFilePath);
                        remove(oldestFilePath.c_str());
                        lastAccessTimes.erase(oldest);
                        std::cout << "Removed image due to disk space limit: " << oldestFilePath << std::endl;
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
