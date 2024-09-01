#ifndef IMAGE_CACHE_MANAGER_H
#define IMAGE_CACHE_MANAGER_H

#include <string>
#include <unordered_map>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

class ImageCacheManager {
public:
    // 构造函数：初始化缓存目录、最大磁盘使用量和缓存文件的最大存活时间
    ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds)
        : cacheDir(cacheDir), maxDiskUsageBytes(maxDiskUsageMB * 1024 * 1024), maxCacheAgeSeconds(maxCacheAgeSeconds) {
        if (!fs::exists(cacheDir)) {
            fs::create_directories(cacheDir);
        }
        cleanerThread = std::thread(&ImageCacheManager::cleanUpOldFiles, this);
    }

    ~ImageCacheManager() {
        stopCleaner = true;
        if (cleanerThread.joinable()) {
            cleanerThread.join();
        }
    }

    // 缓存图片
    void cacheImage(const std::string& fileId, const std::string& imageData) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId);

        std::ofstream file(filePath, std::ios::binary);
        if (file.is_open()) {
            file.write(imageData.c_str(), imageData.size());
            file.close();
            lastAccessTimes[fileId] = std::chrono::system_clock::now();
            std::cout << "Cached image: " << fileId << std::endl;
        }
    }

    // 获取缓存的图片
    std::string getCachedImage(const std::string& fileId) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string filePath = getCacheFilePath(fileId);

        if (fs::exists(filePath)) {
            lastAccessTimes[fileId] = std::chrono::system_clock::now();

            std::ifstream file(filePath, std::ios::binary);
            std::string imageData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return imageData;
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

    // 获取缓存文件的路径
    std::string getCacheFilePath(const std::string& fileId) const {
        return cacheDir + "/" + fileId + ".cache";
    }

    // 获取缓存的总大小
    size_t getCacheSize() const {
        size_t totalSize = 0;
        for (const auto& entry : fs::directory_iterator(cacheDir)) {
            if (fs::is_regular_file(entry.status())) {
                totalSize += fs::file_size(entry.path());
            }
        }
        return totalSize;
    }

    // 清理旧文件的线程
    void cleanUpOldFiles() {
        while (!stopCleaner) {
            std::this_thread::sleep_for(std::chrono::seconds(60)); // 每60秒检查一次

            std::lock_guard<std::mutex> lock(cacheMutex);

            auto now = std::chrono::system_clock::now();
            size_t currentCacheSize = getCacheSize();

            for (auto it = lastAccessTimes.begin(); it != lastAccessTimes.end();) {
                std::string filePath = getCacheFilePath(it->first);

                if (fs::exists(filePath)) {
                    auto fileAge = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();

                    // 删除长时间未访问的文件
                    if (fileAge > maxCacheAgeSeconds) {
                        fs::remove(filePath);
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
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.second < rhs.second;
                    });

                if (oldest != lastAccessTimes.end()) {
                    std::string oldestFilePath = getCacheFilePath(oldest->first);
                    if (fs::exists(oldestFilePath)) {
                        currentCacheSize -= fs::file_size(oldestFilePath);
                        fs::remove(oldestFilePath);
                        lastAccessTimes.erase(oldest);
                        std::cout << "Removed image due to disk space limit: " << oldestFilePath << std::endl;
                    }
                } else {
                    break;
                }
            }
        }
    }
};

#endif // IMAGE_CACHE_MANAGER_H
