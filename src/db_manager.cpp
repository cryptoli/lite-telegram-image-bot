#include "db_manager.h"
#include "utils.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>

DBManager& DBManager::getInstance(const std::string& dbFile, int maxPoolSize, int maxIdleTimeSeconds) {
    static DBManager instance(dbFile, maxPoolSize, maxIdleTimeSeconds);  // 静态局部变量，确保全局唯一
    return instance;
}

// 构造函数私有化
DBManager::DBManager(const std::string& dbFile, int maxPoolSize, int maxIdleTimeSeconds)
    : dbFile(dbFile), maxPoolSize(maxPoolSize), maxIdleTimeSeconds(maxIdleTimeSeconds), stopThread(false) {
    initializePool();  // 初始化连接池
}
void DBManager::initializePool() {
    stopThread.store(false);  // 确保 stopThread 初始化为 false
    cleanupThread = std::thread([this]() {
        while (!stopThread.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(maxIdleTimeSeconds));
            cleanupIdleConnections();
        }
    });
}

DBManager::~DBManager() {
    stopPoolThread();
    if (cleanupThread.joinable()) {
        cleanupThread.join();  // 确保清理线程安全结束
    }
    closeAllConnections();
}

void DBManager::stopPoolThread() {
    stopThread.store(true);  // 将 stopThread 置为 true
}

// 获取连接：从连接池中获取一个连接，如果没有可用连接则动态创建
sqlite3* DBManager::getDbConnection() {
    std::unique_lock<std::mutex> lock(poolMutex);  // 确保线程安全

    // 如果有可用的连接，直接返回
    if (!connectionPool.empty()) {
        sqlite3* db = connectionPool.front();
        connectionPool.pop();
        idleConnections.erase(db);  // 从空闲连接列表中移除
        return db;
    }

    // 如果没有空闲连接且未达到最大连接数，创建一个新的连接
    if (static_cast<int>(connectionPool.size() + idleConnections.size()) < maxPoolSize) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(dbFile.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Can't open database: " + std::string(sqlite3_errmsg(db)));
        } else {
            return db;  // 动态创建新连接并返回
        }
    }

    // 如果连接池已满，等待有连接释放
    poolCondition.wait(lock, [this]() { return !connectionPool.empty(); });

    // 返回空闲的数据库连接
    if (!connectionPool.empty()) {
        sqlite3* db = connectionPool.front();
        connectionPool.pop();
        return db;
    }

    return nullptr;
}

// 释放连接：将连接归还池中，并记录其空闲时间
void DBManager::releaseDbConnection(sqlite3* db) {
    std::unique_lock<std::mutex> lock(poolMutex);
    connectionPool.push(db);
    idleConnections[db] = std::chrono::steady_clock::now();  // 记录空闲时间
    poolCondition.notify_one();  // 通知等待的线程
}

// 定期清理空闲连接：关闭空闲时间超过限制的连接
void DBManager::cleanupIdleConnections() {
    std::unique_lock<std::mutex> lock(poolMutex);
    auto now = std::chrono::steady_clock::now();

    for (auto it = idleConnections.begin(); it != idleConnections.end();) {
        auto idleTime = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (idleTime >= maxIdleTimeSeconds) {
            sqlite3_close(it->first);  // 关闭连接
            it = idleConnections.erase(it);  // 从空闲连接列表中移除
        } else {
            ++it;
        }
    }
}

// 关闭所有连接：关闭池中所有连接
void DBManager::closeAllConnections() {
    std::unique_lock<std::mutex> lock(poolMutex);  // 确保线程安全
    while (!connectionPool.empty()) {
        sqlite3* db = connectionPool.front();
        connectionPool.pop();
        sqlite3_close(db);  // 关闭数据库连接
    }

    for (const auto& idleConn : idleConnections) {
        sqlite3_close(idleConn.first);  // 关闭所有空闲连接
    }
    idleConnections.clear();
}

bool DBManager::createTables() {
    sqlite3* db = getDbConnection();
    char* errMsg = nullptr;
    int rc;

    // Helper lambda function to check if a column exists in a table
    auto columnExists = [&](const std::string& tableName, const std::string& columnName) -> bool {
        std::string query = "PRAGMA table_info(" + tableName + ");";
        bool found = false;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string existingColumn = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (existingColumn == columnName) {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
        return found;
    };

    // Function to add column if it does not exist
    auto addColumnIfNotExists = [&](const std::string& tableName, const std::string& columnName, const std::string& columnDefinition) {
        if (!columnExists(tableName, columnName)) {
            std::string alterSQL = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
            rc = sqlite3_exec(db, alterSQL.c_str(), 0, 0, &errMsg);
            if (rc != SQLITE_OK) {
                log(LogLevel::LOGERROR, "Failed to add column '" + columnName + "' in table '" + tableName + "': " + std::string(errMsg));
                sqlite3_free(errMsg);
                return false;
            }
            log(LogLevel::INFO, "Column '" + columnName + "' added to table '" + tableName + "'.");
        } else {
            log(LogLevel::INFO, "Column '" + columnName + "' already exists in table '" + tableName + "'.");
        }
        return true;
    };

    // 创建用户表，如果不存在则创建
    log(LogLevel::INFO, "Creating or updating users table...");
    const char* userTableSQL = "CREATE TABLE IF NOT EXISTS users ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "telegram_id INTEGER UNIQUE, "
                               "username TEXT, "
                               "is_banned BOOLEAN DEFAULT 0, "
                               "created_at TEXT DEFAULT (datetime('now')), "
                               "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(db, userTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "Users table created or exists already.");

    // 检查并添加用户表中的字段
    addColumnIfNotExists("users", "id", "INTEGER PRIMARY KEY AUTOINCREMENT");
    addColumnIfNotExists("users", "telegram_id", "INTEGER UNIQUE");
    addColumnIfNotExists("users", "username", "TEXT");
    addColumnIfNotExists("users", "is_banned", "BOOLEAN DEFAULT 0");
    addColumnIfNotExists("users", "created_at", "TEXT DEFAULT (datetime('now'))");
    addColumnIfNotExists("users", "updated_at", "TEXT DEFAULT (datetime('now'))");

    // 创建用户表的触发器，更新 `updated_at` 字段
    const char* userTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_user_timestamp "
                                      "AFTER UPDATE ON users "
                                      "FOR EACH ROW "
                                      "BEGIN "
                                      "UPDATE users SET updated_at = datetime('now') WHERE id = OLD.id; "
                                      "END;";
    rc = sqlite3_exec(db, userTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "User table trigger created or exists already.");

    // 创建文件表，如果不存在则创建
    log(LogLevel::INFO, "Creating or updating files table...");
    const char* fileTableSQL = "CREATE TABLE IF NOT EXISTS files ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "user_id INTEGER, "
                               "file_id TEXT, "
                               "file_link TEXT, "
                               "file_name TEXT, "
                               "short_id TEXT,"
                               "short_link TEXT,"
                               "extension TEXT, "
                               "is_valid BOOLEAN DEFAULT 1, "
                               "created_at TEXT DEFAULT (datetime('now')), "
                               "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(db, fileTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "Files table created or exists already.");

    // 检查并添加文件表中的字段
    addColumnIfNotExists("files", "id", "INTEGER PRIMARY KEY AUTOINCREMENT");
    addColumnIfNotExists("files", "user_id", "INTEGER");
    addColumnIfNotExists("files", "file_id", "TEXT");
    addColumnIfNotExists("files", "file_link", "TEXT");
    addColumnIfNotExists("files", "file_name", "TEXT");
    addColumnIfNotExists("files", "extension", "TEXT");
    addColumnIfNotExists("files", "short_id", "TEXT");
    addColumnIfNotExists("files", "short_link", "TEXT");
    addColumnIfNotExists("files", "is_valid", "BOOLEAN DEFAULT 1");
    addColumnIfNotExists("files", "created_at", "TEXT DEFAULT (datetime('now'))");
    addColumnIfNotExists("files", "updated_at", "TEXT DEFAULT (datetime('now'))");

    // 为 short_id 和 file_id 创建索引
    const char* fileIndexSQL = "CREATE INDEX IF NOT EXISTS idx_files_short_id ON files(short_id);"
                               "CREATE INDEX IF NOT EXISTS idx_files_file_id ON files(file_id);";
    rc = sqlite3_exec(db, fileIndexSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table Index): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "Indexes for files table created or exist already.");

    // 创建文件表的触发器，更新 `updated_at` 字段
    const char* fileTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_file_timestamp "
                                      "AFTER UPDATE ON files "
                                      "FOR EACH ROW "
                                      "BEGIN "
                                      "UPDATE files SET updated_at = datetime('now') WHERE id = OLD.id; "
                                      "END;";
    rc = sqlite3_exec(db, fileTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "File table trigger created or exists already.");

    // 创建设置表，如果不存在则创建
    log(LogLevel::INFO, "Creating or updating settings table...");
    const char* settingsTableSQL = "CREATE TABLE IF NOT EXISTS settings ("
                                   "key TEXT PRIMARY KEY, "
                                   "value TEXT, "
                                   "created_at TEXT DEFAULT (datetime('now')), "
                                   "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(db, settingsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "Settings table created or exists already.");

    // 检查并添加 settings 表的字段
    addColumnIfNotExists("settings", "webhook_url", "TEXT");
    addColumnIfNotExists("settings", "allow_registration", "INTEGER DEFAULT 0");
    addColumnIfNotExists("settings", "ssl_key", "TEXT");
    addColumnIfNotExists("settings", "ssl_certificate", "TEXT");
    addColumnIfNotExists("settings", "use_https", "INTEGER DEFAULT 0");
    addColumnIfNotExists("settings", "api_token", "TEXT");
    addColumnIfNotExists("settings", "secret_token", "TEXT");
    addColumnIfNotExists("settings", "owner_id", "INTEGER");
    addColumnIfNotExists("settings", "telegram_api_url", "TEXT");
    addColumnIfNotExists("settings", "max_cache_size", "INTEGER DEFAULT 1024");
    addColumnIfNotExists("settings", "max_cache_age_seconds", "INTEGER DEFAULT 86400");
    addColumnIfNotExists("settings", "enable_referers", "INTEGER DEFAULT 0");
    addColumnIfNotExists("settings", "allowed_referers", "TEXT");  // 可以使用逗号分隔的字符串存储多个 referer
    addColumnIfNotExists("settings", "requests_per_minute", "INTEGER DEFAULT 60");

    // 创建设置表的触发器，更新 `updated_at` 字段
    const char* settingsTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_settings_timestamp "
                                          "AFTER UPDATE ON settings "
                                          "FOR EACH ROW "
                                          "BEGIN "
                                          "UPDATE settings SET updated_at = datetime('now') WHERE key = OLD.key; "
                                          "END;";
    rc = sqlite3_exec(db, settingsTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        releaseDbConnection(db);
        return false;
    }
    log(LogLevel::INFO, "Settings table trigger created or exists already.");
    releaseDbConnection(db);
    return true;
}

bool DBManager::isUserRegistered(const std::string& telegramId) {
    sqlite3* db = getDbConnection();

    std::string query = "SELECT COUNT(*) FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool userExists = false;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            userExists = (count > 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return userExists;
}

bool DBManager::addUserIfNotExists(const std::string& telegramId, const std::string& username) {
    sqlite3* db = getDbConnection();

    if (isUserRegistered(telegramId)) {
        releaseDbConnection(db);
        return true;  // 用户已经存在
    }

    int rc = sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to begin transaction.");
        releaseDbConnection(db);
        return false;
    }

    std::string insertSQL = "INSERT INTO users (telegram_id, username) VALUES (?, ?)";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(db, insertSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement: " + std::string(sqlite3_errmsg(db)));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        releaseDbConnection(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert user.");
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        releaseDbConnection(db);
        return false;
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    log(LogLevel::INFO, "New user inserted successfully.");
    releaseDbConnection(db);
    return true;
}

bool DBManager::addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName, const std::string& shortId, const std::string& shortLink, const std::string& extension) {
    sqlite3* db = getDbConnection();
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // 首先检查 file_id 是否已经存在
    std::string checkFileSQL = "SELECT COUNT(*) FROM files WHERE file_id = ?";
    sqlite3_stmt* checkStmt;
    int rc = sqlite3_prepare_v2(db, checkFileSQL.c_str(), -1, &checkStmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement addFile (File Check): " + std::string(sqlite3_errmsg(db)));
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        releaseDbConnection(db);
        return false;
    }

    // 绑定 file_id 参数
    sqlite3_bind_text(checkStmt, 1, fileId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(checkStmt);
    
    if (rc != SQLITE_ROW) {
        log(LogLevel::LOGERROR, "Failed to step SELECT statement: " + std::string(sqlite3_errmsg(db)));
        sqlite3_finalize(checkStmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        releaseDbConnection(db);
        return false;
    }

    int fileExists = sqlite3_column_int(checkStmt, 0); // 如果大于0，表示文件已存在
    sqlite3_finalize(checkStmt);

    if (fileExists > 0) {
        // 如果文件已存在，执行更新操作
        std::string updateFileSQL = R"(
            UPDATE files SET 
                file_link = ?,
                file_name = ?,
                short_id = ?,
                short_link = ?,
                extension = ?
            WHERE file_id = ?
        )";
        sqlite3_stmt* updateStmt;
        rc = sqlite3_prepare_v2(db, updateFileSQL.c_str(), -1, &updateStmt, nullptr);
        if (rc != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (File): " + std::string(sqlite3_errmsg(db)));
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            releaseDbConnection(db);
            return false;
        }

        // 绑定更新语句的参数
        sqlite3_bind_text(updateStmt, 1, fileLink.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 2, fileName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 3, shortId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 4, shortLink.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 5, extension.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(updateStmt, 6, fileId.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(updateStmt);
        sqlite3_finalize(updateStmt);

        if (rc != SQLITE_DONE) {
            log(LogLevel::LOGERROR, "Failed to update file record: " + std::string(sqlite3_errmsg(db)));
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            releaseDbConnection(db);
            return false;
        }
    } else {
        // 如果文件不存在，执行插入操作
        std::string insertFileSQL = R"(
            INSERT INTO files (user_id, file_id, file_link, file_name, short_id, short_link, extension)
            VALUES ((SELECT id FROM users WHERE telegram_id = ?), ?, ?, ?, ?, ?, ?)
        )";
        sqlite3_stmt* insertStmt;
        rc = sqlite3_prepare_v2(db, insertFileSQL.c_str(), -1, &insertStmt, nullptr);
        if (rc != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Failed to prepare INSERT statement (File): " + std::string(sqlite3_errmsg(db)));
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            releaseDbConnection(db);
            return false;
        }

        // 绑定插入语句的参数
        sqlite3_bind_text(insertStmt, 1, userId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 2, fileId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 3, fileLink.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 4, fileName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 5, shortId.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 6, shortLink.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(insertStmt, 7, extension.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(insertStmt);
        sqlite3_finalize(insertStmt);

        if (rc != SQLITE_DONE) {
            log(LogLevel::LOGERROR, "Failed to insert file record: " + std::string(sqlite3_errmsg(db)));
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            releaseDbConnection(db);
            return false;
        }
    }

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    log(LogLevel::INFO, "File record inserted or updated successfully.");
    releaseDbConnection(db);
    return true;
}

bool DBManager::removeFile(const std::string& userId, const std::string& fileName) {
    sqlite3* db = getDbConnection();

    std::string deleteSQL = "DELETE FROM files "
                            "WHERE id = ? AND user_id IN (SELECT id FROM users WHERE telegram_id = ?)";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare DELETE statement (File): " + std::string(sqlite3_errmsg(db)));
        releaseDbConnection(db);
        return false;
    }

    log(LogLevel::INFO, "Attempting to delete file with userId: " + userId + " and fileName: " + fileName);

    sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to delete file record: " + std::string(sqlite3_errmsg(db)) + " Error code: " + std::to_string(rc));
        sqlite3_finalize(stmt);
        releaseDbConnection(db);
        return false;
    }

    int changes = sqlite3_changes(db);
    if (changes == 0) {
        log(LogLevel::WARNING, "No records were deleted. Either the user or file was not found.");
        sqlite3_finalize(stmt);
        releaseDbConnection(db);
        return false;
    }

    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "File record deleted successfully.");
    releaseDbConnection(db);
    return true;
}

bool DBManager::banUser(const std::string& telegramId) {
    sqlite3* db = getDbConnection();
    std::string updateSQL = "UPDATE users SET is_banned = 1 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Ban): " + std::string(sqlite3_errmsg(db)));
        releaseDbConnection(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    releaseDbConnection(db);
    return rc == SQLITE_DONE;
}

bool DBManager::unbanUser(const std::string& telegramId) {
    sqlite3* db = getDbConnection();
    std::string updateSQL = "UPDATE users SET is_banned = 0 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Unban): " + std::string(sqlite3_errmsg(db)));
        releaseDbConnection(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    releaseDbConnection(db);
    return rc == SQLITE_DONE;
}

std::vector<std::tuple<std::string, std::string, std::string>> DBManager::getUserFiles(const std::string& userId, int page, int pageSize) {
    sqlite3* db = getDbConnection();
    std::vector<std::tuple<std::string, std::string, std::string>> files;
    std::string selectSQL = "SELECT file_name, file_link, id FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ? ORDER BY updated_at DESC) LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, pageSize);
    sqlite3_bind_int(stmt, 3, (page - 1) * pageSize);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string fileLink = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        files.emplace_back(fileName, fileLink, fileId);
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Fetched " + std::to_string(files.size()) + " files for user ID: " + userId + " (Page: " + std::to_string(page) + ")");
    releaseDbConnection(db);
    return files;
}

int DBManager::getUserFileCount(const std::string& userId) {
    sqlite3* db = getDbConnection();
    std::string query = "SELECT COUNT(*) FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?)";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return count;
}

std::string DBManager::getFileIdByShortId(const std::string& shortId) {
    sqlite3* db = getDbConnection();
    std::string query = "SELECT file_id FROM files WHERE short_id = ? LIMIT 1";  // 添加 LIMIT 1 只取第一条
    sqlite3_stmt* stmt;
    std::string fileId;  // 初始化为空字符串

    // 准备 SQL 语句
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 绑定 shortId 参数
        sqlite3_bind_text(stmt, 1, shortId.c_str(), -1, SQLITE_STATIC);

        // 执行查询并获取结果
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // 使用 sqlite3_column_text 获取字符串数据
            const unsigned char* result = sqlite3_column_text(stmt, 0);
            if (result != nullptr) {
                fileId = reinterpret_cast<const char*>(result);  // 将返回值转换为 std::string
            }
        }

        sqlite3_finalize(stmt);  // 释放 stmt 资源
    } else {
        // 如果查询失败，打印错误日志
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }

    // 打印日志
    log(LogLevel::INFO, "Select file_id by short_id: " + shortId + ", file ID: " + fileId);
    releaseDbConnection(db);
    return fileId;
}

void DBManager::setRegistrationOpen(bool isOpen) {
    sqlite3* db = getDbConnection();

    std::string updateSQL = "INSERT OR REPLACE INTO settings (key, value) VALUES ('registration', ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, isOpen ? "1" : "0", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to update registration setting: " + std::string(sqlite3_errmsg(db)));
    } else {
        log(LogLevel::INFO, "Registration setting updated successfully.");
    }
}

bool DBManager::isRegistrationOpen() {
    sqlite3* db = getDbConnection();

    std::string selectSQL = "SELECT value FROM settings WHERE key = 'registration'";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        bool isOpen = value == "1";
        log(LogLevel::INFO, "Registration is " + std::string(isOpen ? "open" : "closed") + ".");
        releaseDbConnection(db);
        return isOpen;
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Registration status not found, defaulting to open.");
    releaseDbConnection(db);
    return true;
}

int DBManager::getTotalUserCount() {
    sqlite3* db = getDbConnection();
    std::string query = "SELECT COUNT(*) FROM users";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return count;
}

std::vector<std::tuple<std::string, std::string, bool>> DBManager::getUsersForBan(int page, int pageSize) {
    sqlite3* db = getDbConnection();
    std::vector<std::tuple<std::string, std::string, bool>> users;
    std::string selectSQL = "SELECT telegram_id, username, is_banned FROM users ORDER BY updated_at DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, pageSize);
        sqlite3_bind_int(stmt, 2, (page - 1) * pageSize);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string telegramId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            bool isBanned = sqlite3_column_int(stmt, 2) == 1;
            users.emplace_back(telegramId, username, isBanned);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return users;
}

bool DBManager::isUserBanned(const std::string& telegramId) {
    sqlite3* db = getDbConnection();
    std::string query = "SELECT is_banned FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool isBanned = false;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            isBanned = sqlite3_column_int(stmt, 0) == 1;
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return isBanned;
}
std::vector<std::tuple<std::string, std::string, std::string, std::string>> DBManager::getImagesAndVideos(int page, int pageSize) {
    sqlite3* db = getDbConnection();
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> files;
    int offset = (page - 1) * pageSize;

    // 记录开始查询的日志
    log(LogLevel::INFO, "Starting image and video query. Page: " + std::to_string(page) + ", Page Size: " + std::to_string(pageSize));

    // 包含 extension 字段的查询
    std::string selectSQL = R"(
        SELECT DISTINCT file_id, file_name, file_link, extension
        FROM files 
        WHERE extension IN (
            '.jpg', '.jpeg', '.png', '.gif', '.bmp', '.webp', '.tiff', '.tif', '.svg', '.heic',  -- 图片格式
            '.mp4', '.mkv', '.avi', '.mov', '.flv', '.wmv', '.webm', '.m4v', '.3gp', '.hevc', '.ts' -- 视频格式
        )
        ORDER BY updated_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, selectSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        // 记录 SQL 语句准备成功
        log(LogLevel::INFO, "SQL statement prepared successfully.");

        // 绑定分页参数
        sqlite3_bind_int(stmt, 1, pageSize);
        sqlite3_bind_int(stmt, 2, offset);

        int rowCount = 0; // 用于记录返回的行数

        // 提取数据并记录每条记录
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string fileId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string fileName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            std::string fileLink = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            std::string extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            files.emplace_back(fileId, fileName, fileLink, extension);

            // 记录每次提取的文件信息
            log(LogLevel::INFO, "Fetched file: ID = " + fileId + ", Name = " + fileName + ", Link = " + fileLink + ", Extension = " + extension);
            rowCount++;
        }
        
        sqlite3_finalize(stmt);

        // 记录查询结果数量
        log(LogLevel::INFO, "Query completed. Fetched " + std::to_string(rowCount) + " files.");
    } else {
        // 记录 SQL 语句准备失败的错误
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(db)));
    }
    releaseDbConnection(db);
    return files;
}