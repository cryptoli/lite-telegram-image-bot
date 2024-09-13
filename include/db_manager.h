#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <string>
#include <vector>
#include <tuple>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <sqlite3.h>
#include <atomic>
#include <thread>

class DBManager {
public:
    static DBManager& getInstance(const std::string& dbFile = "bot_database.db", int maxPoolSize = 20, int maxIdleTimeSeconds = 60);

    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

    bool initialize();
    bool createTables();
    bool addUserIfNotExists(const std::string& telegramId, const std::string& username);
    bool addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName, const std::string& shortId, const std::string& shortLink, const std::string& extension);
    bool removeFile(const std::string& userId, const std::string& fileId);
    bool banUser(const std::string& telegramId);
    bool unbanUser(const std::string& telegramId);
    bool isUserRegistered(const std::string& telegramId);
    bool isUserBanned(const std::string& telegramId);
    bool isRegistrationOpen();
    void setRegistrationOpen(bool isOpen);
    
    int getUserFileCount(const std::string& userId);
    int getTotalUserCount();
    std::vector<std::tuple<std::string, std::string, std::string>> getUserFiles(const std::string& userId, int page, int pageSize);
    std::vector<std::tuple<std::string, std::string, bool>> getUsersForBan(int page, int pageSize);
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> getImagesAndVideos(int page, int pageSize);
    std::string getFileIdByShortId(const std::string& shortId);

private:
    std::string dbFile;
    int maxPoolSize;
    int maxIdleTimeSeconds;
    std::atomic<bool> stopThread;
    std::thread cleanupThread;

    DBManager(const std::string& dbFile, int maxPoolSize, int maxIdleTimeSeconds);
    ~DBManager();

    std::queue<sqlite3*> connectionPool;
    std::unordered_map<sqlite3*, std::chrono::steady_clock::time_point> idleConnections;  // 存储连接的空闲时间
    std::mutex poolMutex;
    std::condition_variable poolCondition;

    sqlite3* getDbConnection();
    void releaseDbConnection(sqlite3* db);
    void initializePool();
    void cleanupIdleConnections();
    void closeAllConnections();
     void stopPoolThread();
};

#endif