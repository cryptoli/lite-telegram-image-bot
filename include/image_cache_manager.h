#ifndef IMAGE_CACHE_MANAGER_H
#define IMAGE_CACHE_MANAGER_H

#include <string>
#include <unordered_map>
#include <chrono>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <algorithm>

namespace fs = boost::filesystem;

class ImageCacheManager {
public:
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

        if (fs::exists(filePath)) {
            lastAccessTimes[fileId] = std::chrono::system_clock::now();

            std::ifstream file(filePath.c_str(), std::ios::binary);
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

    std::string getCacheFilePath(const std::string& fileId) const {
        return cacheDir + "/" + fileId + ".cache";
    }

    size_t getCacheSize() const {
        size_t totalSize = 0;
        for (fs::directory_iterator itr(cacheDir); itr != fs::directory_iterator(); ++itr) {
            if (fs::is_regular_file(itr->status())) {
                totalSize += fs::file_size(itr->path());
            }
        }
        return totalSize;
    }

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
                    [](const std::pair<const std::string, std::chrono::system_clock::time_point>& lhs,
                       const std::pair<const std::string, std::chrono::system_clock::time_point>& rhs) {
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

#endif 
