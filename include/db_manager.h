#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <string>
#include <sqlite3.h>
#include <vector>
#include <utility>

class DBManager {
public:
    DBManager(const std::string& dbFile);
    ~DBManager();

    // 初始化数据库
    bool initialize();

    // 添加用户（如果不存在）
    bool addUserIfNotExists(const std::string& telegramId, const std::string& username);

    // 添加文件记录
    bool addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName);

    // 删除文件记录
    bool removeFile(const std::string& userId, const std::string& fileName);

    // 封禁用户
    bool banUser(const std::string& telegramId);

    // 获取用户收集的文件列表
    std::vector<std::pair<std::string, std::string>> getUserFiles(const std::string& userId);

    // 设置是否允许注册
    void setRegistrationOpen(bool isOpen);

    // 检查是否允许注册
    bool isRegistrationOpen();

private:
    // 创建表
    bool createTables();

    sqlite3* db;
    std::string dbFile;
};

#endif
