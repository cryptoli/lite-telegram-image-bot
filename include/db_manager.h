#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include "dynamic_sqlite_thread_pool.h"
#include <string>
#include <vector>
#include <utility>
#include <sqlite3.h>

class DBManager {
public:
    DBManager(const std::string& dbFile, int minPoolSize = 2, int maxPoolSize = 20);
    ~DBManager();

    // 初始化数据库，创建表结构
    bool initialize();

    // 添加用户，如果用户不存在则添加
    bool addUserIfNotExists(const std::string& telegramId, const std::string& username);

    // 检查用户是否已注册
    bool isUserRegistered(const std::string& telegramId);

    // 添加文件记录
    bool addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName);

    // 删除文件记录
    bool removeFile(const std::string& userId, const std::string& fileName);

    // 获取用户的文件列表
    std::vector<std::tuple<std::string, std::string, std::string>> getUserFiles(const std::string& userId, int page, int pageSize);

    int getUserFileCount(const std::string& userId);

    // 封禁用户
    bool banUser(const std::string& telegramId);
    bool unbanUser(const std::string& telegramId);

    // 设置是否允许注册
    void setRegistrationOpen(bool isOpen);

    // 检查是否允许注册
    bool isRegistrationOpen();
    int getTotalUserCount();
    std::vector<std::tuple<std::string, std::string, bool>> getUsersForBan(int page, int pageSize);
    bool isUserBanned(const std::string& telegramId);

private:
    std::string dbFile;
    DynamicSQLiteThreadPool threadPool;

    // 创建所需的表
    bool createTables();

    // 执行SQL语句的通用函数
    bool executeSQL(const std::string& query);
};

#endif