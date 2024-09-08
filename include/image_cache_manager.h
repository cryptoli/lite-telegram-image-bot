#ifndef IMAGE_CACHE_MANAGER_H
#define IMAGE_CACHE_MANAGER_H

#include "utils.h"
#include <string>
#include <unordered_map>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <direct.h>
#include <windows.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#endif

class ImageCacheManager {
public:
    ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds)
        : maxDiskUsageBytes(maxDiskUsageMB * 1024 * 1024), maxCacheAgeSeconds(maxCacheAgeSeconds) {

        // 将相对路径转换为绝对路径
        char absolutePath[PATH_MAX];
#ifdef _WIN32
        if (_fullpath(absolutePath, cacheDir.c_str(), PATH_MAX) != nullptr) {
#else
        if (realpath(cacheDir.c_str(), absolutePath) != nullptr) {
#endif
            this->cacheDir = std::string(absolutePath);
        } else {
            this->cacheDir = cacheDir;
            log(LogLevel::LOGERROR, "Failed to resolve cache directory to absolute path: " + cacheDir);
        }

        if (!directoryExists(this->cacheDir)) {
#ifdef _WIN32
            _mkdir(this->cacheDir.c_str());
#else
            mkdir(this->cacheDir.c_str(), 0777);
#endif
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
            log(LogLevel::LOGERROR, "Failed to open file for caching: " + filePath);
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
                log(LogLevel::LOGERROR, "Failed to open cached file: " + filePath);
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
#ifdef _WIN32
        return cacheDir + "\\" + fileId + extension;
#else
        return cacheDir + "/" + fileId + extension;
#endif
    }

    size_t getCacheSize() const {
        size_t totalSize = 0;
#ifdef _WIN32
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile((cacheDir + "\\*").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    totalSize += (findFileData.nFileSizeHigh * (MAXDWORD + 1)) + findFileData.nFileSizeLow;
                }
            } while (FindNextFile(hFind, &findFileData) != 0);
            FindClose(hFind);
        }
#else
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
#endif
        return totalSize;
    }

    bool directoryExists(const std::string& path) {
#ifdef _WIN32
        struct _stat64i32 info;
        if (_stat64i32(path.c_str(), &info) != 0) {
#else
        struct stat info;
        if (stat(path.c_str(), &info) != 0) {
#endif
            return false;
        } else if (info.st_mode & S_IFDIR) {
            return true;
        } else {
            return false;
        }
    }

    bool fileExists(const std::string& path) {
#ifdef _WIN32
        struct _stat64i32 buffer;
        return (_stat64i32(path.c_str(), &buffer) == 0);
#else
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
#endif
    }

    void cleanUpOldFiles() {
    while (!stopCleaner) {
        std::this_thread::sleep_for(std::chrono::seconds(600)); // 每6秒检查一次
        log(LogLevel::INFO, "Check cache file state...");

        std::lock_guard<std::mutex> lock(cacheMutex);

        auto now = std::chrono::system_clock::now();
        size_t currentCacheSize = getCacheSize();
        
        log(LogLevel::INFO, "Current cache size: " + std::to_string(currentCacheSize));
        log(LogLevel::INFO, "Number of files in lastAccessTimes: " + std::to_string(lastAccessTimes.size()));

        for (auto it = lastAccessTimes.begin(); it != lastAccessTimes.end();) {
            std::string filePath = getCacheFilePath(it->first, "");
            log(LogLevel::INFO, "Check file: " + filePath);

            if (fileExists(filePath)) {
                auto fileAge = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                log(LogLevel::INFO, "File age: " + std::to_string(fileAge) + " seconds");

                // 删除长时间未访问的文件
                if (fileAge > maxCacheAgeSeconds) {
                    remove(filePath.c_str());
                    it = lastAccessTimes.erase(it);
                    log(LogLevel::INFO, "Removed old cached image: " + filePath);
                } else {
                    ++it;
                }
            } else {
                log(LogLevel::INFO, "File not found, removing from lastAccessTimes: " + filePath);
                it = lastAccessTimes.erase(it);
            }
        }

        // 检查磁盘使用情况并清理缓存
        while (currentCacheSize > maxDiskUsageBytes) {
            log(LogLevel::INFO, "Cache size exceeds limit, starting removal...");
            auto oldest = std::min_element(lastAccessTimes.begin(), lastAccessTimes.end(),
                [](const std::pair<const std::string, std::chrono::system_clock::time_point>& lhs,
                   const std::pair<const std::string, std::chrono::system_clock::time_point>& rhs) {
                    return lhs.second < rhs.second;
                });

            if (oldest != lastAccessTimes.end()) {
                std::string oldestFilePath = getCacheFilePath(oldest->first, "");
                if (fileExists(oldestFilePath)) {
                    log(LogLevel::INFO, "Removing file due to disk space limit: " + oldestFilePath);
                    currentCacheSize -= getFileSize(oldestFilePath);
                    remove(oldestFilePath.c_str());
                    lastAccessTimes.erase(oldest);
                }
            } else {
                break;
            }
        }
    }
}


    size_t getFileSize(const std::string& path) {
#ifdef _WIN32
        struct _stat64i32 filestat;
        if (_stat64i32(path.c_str(), &filestat) == 0) {
#else
        struct stat filestat;
        if (stat(path.c_str(), &filestat) == 0) {
#endif
            return filestat.st_size;
        }
        return 0;
    }
};

#endif
