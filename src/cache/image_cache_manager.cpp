#include "image_cache_manager.h"
#include "utils.h"
#include <fstream>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#endif

ImageCacheManager::ImageCacheManager(const std::string& cacheDir, size_t maxDiskUsageMB, int maxCacheAgeSeconds)
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
        log(LogLevel::LOGERROR, "Failed to resolve cache directory to absolute path: ", cacheDir);
    }

    if (!directoryExists(this->cacheDir)) {
#ifdef _WIN32
        _mkdir(this->cacheDir.c_str());
#else
        mkdir(this->cacheDir.c_str(), 0777);
#endif
        log(LogLevel::INFO, "Created cache directory: ", this->cacheDir);
    }

    cleanerThread = std::thread(&ImageCacheManager::cleanUpFilesOnDiskSpaceLimit, this);
}

ImageCacheManager::~ImageCacheManager() {
    stopCleaner = true;
    if (cleanerThread.joinable()) {
        cleanerThread.join();
    }
    log(LogLevel::INFO, "Cache manager cleaned up and exited.");
}

void ImageCacheManager::cacheImage(const std::string& fileId, const std::string& imageData, const std::string& extension) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::string filePath = getCacheFilePath(fileId, extension);

    std::ofstream file(filePath.c_str(), std::ios::binary);
    if (file.is_open()) {
        file.write(imageData.c_str(), imageData.size());
        file.close();
        log(LogLevel::INFO, "Cached image: ", fileId, " at ", filePath);

        // 检查缓存大小是否超出限制
        cleanUpFilesOnDiskSpaceLimit();
    } else {
        log(LogLevel::LOGERROR, "Failed to open file for caching: ", filePath);
    }
}

std::string ImageCacheManager::getCachedImage(const std::string& fileId, const std::string& extension) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    std::string filePath = getCacheFilePath(fileId, extension);

    if (fileExists(filePath)) {
        std::ifstream file(filePath.c_str(), std::ios::binary);
        if (file.is_open()) {
            std::string imageData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            log(LogLevel::INFO, "Cache hit: ", fileId, " from ", filePath);
            return imageData;
        } else {
            log(LogLevel::LOGERROR, "Failed to open cached file: ", filePath);
        }
    } else {
        log(LogLevel::WARNING, "Cache miss for file ID: ", fileId);
    }

    return "";
}

std::string ImageCacheManager::getCacheFilePath(const std::string& fileId, const std::string& extension) const {
#ifdef _WIN32
    return cacheDir + "\\" + fileId + extension;
#else
    return cacheDir + "/" + fileId + extension;
#endif
}

size_t ImageCacheManager::getCacheSize() const {
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

bool ImageCacheManager::directoryExists(const std::string& path) {
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

bool ImageCacheManager::fileExists(const std::string& path) {
#ifdef _WIN32
    struct _stat64i32 buffer;
    return (_stat64i32(path.c_str(), &buffer) == 0);
#else
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
#endif
}

size_t ImageCacheManager::getFileSize(const std::string& path) {
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

std::chrono::system_clock::time_point ImageCacheManager::getFileModificationTime(const std::string& filePath) {
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == 0) {
        return std::chrono::system_clock::from_time_t(fileStat.st_mtime);
    }
    return std::chrono::system_clock::now();  // 如果无法获取修改时间，返回当前时间
}

void ImageCacheManager::cleanUpFilesOnDiskSpaceLimit() {
    size_t currentCacheSize = getCacheSize();

    // 当缓存大小超过限制时进行清理
    while (currentCacheSize > maxDiskUsageBytes) {
        // 获取缓存目录下所有文件
        std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> files;

#ifdef _WIN32
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile((cacheDir + "\\*").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string filePath = cacheDir + "\\" + findFileData.cFileName;
                    files.push_back({filePath, getFileModificationTime(filePath)});
                }
            } while (FindNextFile(hFind, &findFileData) != 0);
            FindClose(hFind);
        }
#else
        DIR* dirp = opendir(cacheDir.c_str());
        struct dirent* entry;
        while ((entry = readdir(dirp)) != nullptr) {
            if (entry->d_type == DT_REG) {  // 仅处理常规文件
                std::string filePath = cacheDir + "/" + entry->d_name;
                files.push_back({filePath, getFileModificationTime(filePath)});
            }
        }
        closedir(dirp);
#endif

        // 根据文件的修改时间排序，最旧的文件排在前面
        std::sort(files.begin(), files.end(), [](const std::pair<std::string, std::chrono::system_clock::time_point>& lhs,
                                                 const std::pair<std::string, std::chrono::system_clock::time_point>& rhs) {
            return lhs.second < rhs.second;
        });

        // 删除最旧的文件，直到缓存大小小于限制
        for (const auto& file : files) {
            if (currentCacheSize <= maxDiskUsageBytes) {
                break;
            }

            std::string filePath = file.first;
            size_t fileSize = getFileSize(filePath);
            if (remove(filePath.c_str()) == 0) {
                currentCacheSize -= fileSize;
                log(LogLevel::INFO, "Removed image due to disk space limit: ", filePath);
            } else {
                log(LogLevel::LOGERROR, "Failed to remove file: ", filePath);
            }
        }

        // 如果文件夹中没有文件需要删除，退出循环
        if (files.empty()) {
            log(LogLevel::INFO, "No files found for deletion.");
            break;
        }
    }
}
