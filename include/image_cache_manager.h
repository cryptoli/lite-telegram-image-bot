#ifndef IMAGE_CACHE_MANAGER_H
#define IMAGE_CACHE_MANAGER_H

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

// 类的定义
class ImageCacheManager {
public:
    ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds);
    ~ImageCacheManager();
    
    void cacheImage(const std::string& fileId, const std::string& imageData, const std::string& extension);
    std::string getCachedImage(const std::string& fileId, const std::string& extension);

private:
    std::string cacheDir;
    size_t maxDiskUsageBytes;
    int maxCacheAgeSeconds;
    std::thread cleanerThread;
    bool stopCleaner = false;
    std::mutex cacheMutex;

    size_t getCacheSize() const;
    std::string getCacheFilePath(const std::string& fileId, const std::string& extension) const;
    bool directoryExists(const std::string& path);
    bool fileExists(const std::string& path);
    size_t getFileSize(const std::string& path);

    std::chrono::system_clock::time_point getFileModificationTime(const std::string& filePath);
    void cleanUpFilesOnDiskSpaceLimit();
};

#endif
