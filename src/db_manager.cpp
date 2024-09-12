#include "db_manager.h"
#include "utils.h"
#include <iostream>

DBManager::DBManager(const std::string& dbFile, int minPoolSize, int maxPoolSize) : dbFile(dbFile), threadPool(dbFile, minPoolSize, maxPoolSize) {

}

DBManager::~DBManager() {

}

bool DBManager::initialize() {
    sqlite3* conn = threadPool.acquireConnection();
    if (!conn) {
        log(LogLevel::LOGERROR, "Failed to initialize database.");
        return false;
    }

    std::string query = "PRAGMA journal_mode=WAL;";
    char* errMsg = nullptr;
    if (sqlite3_exec(conn, query.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to set journal mode: " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
        return false;
    }
    threadPool.releaseConnection(conn);

    return createTables();
}

bool DBManager::createTables() {
    sqlite3* conn = threadPool.acquireConnection();
    char* errMsg = nullptr;
    int rc;

    // Helper lambda function to check if a column exists in a table
    auto columnExists = [&](const std::string& tableName, const std::string& columnName) -> bool {
        sqlite3* conn = threadPool.acquireConnection();
        std::string query = "PRAGMA table_info(" + tableName + ");";
        bool found = false;
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string existingColumn = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                if (existingColumn == columnName) {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(stmt);
        }
        threadPool.releaseConnection(conn);
        return found;
    };

    // Function to add column if it does not exist
    auto addColumnIfNotExists = [&](const std::string& tableName, const std::string& columnName, const std::string& columnDefinition) {
        sqlite3* conn = threadPool.acquireConnection();
        if (!columnExists(tableName, columnName)) {
            std::string alterSQL = "ALTER TABLE " + tableName + " ADD COLUMN " + columnName + " " + columnDefinition + ";";
            rc = sqlite3_exec(conn, alterSQL.c_str(), 0, 0, &errMsg);
            if (rc != SQLITE_OK) {
                log(LogLevel::LOGERROR, "Failed to add column '" + columnName + "' in table '" + tableName + "': " + std::string(errMsg));
                sqlite3_free(errMsg);
                threadPool.releaseConnection(conn);
                return false;
            }
            log(LogLevel::INFO, "Column '" + columnName + "' added to table '" + tableName + "'.");
        } else {
            log(LogLevel::INFO, "Column '" + columnName + "' already exists in table '" + tableName + "'.");
        }
        threadPool.releaseConnection(conn);
        return true;
    };
    
    // 创建用户表
    log(LogLevel::INFO, "Creating user table...");
    const char* userTableSQL = "CREATE TABLE IF NOT EXISTS users ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "telegram_id INTEGER UNIQUE, "
                               "username TEXT, "
                               "is_banned BOOLEAN DEFAULT 0, "
                               "created_at TEXT DEFAULT (datetime('now')), "
                               "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(conn, userTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
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
    rc = sqlite3_exec(conn, userTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
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
    rc = sqlite3_exec(conn, fileTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
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
    rc = sqlite3_exec(conn, fileIndexSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table Index): " + std::string(errMsg));
        sqlite3_free(errMsg);
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
    rc = sqlite3_exec(conn, fileTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
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
    rc = sqlite3_exec(conn, settingsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
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
    rc = sqlite3_exec(conn, settingsTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        threadPool.releaseConnection(conn);
        return false;
    }
    threadPool.releaseConnection(conn);
    log(LogLevel::INFO, "Settings table trigger created or exists already.");

    return true;
}

bool DBManager::isUserRegistered(const std::string& telegramId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string query = "SELECT COUNT(*) FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool userExists = false;

    if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            userExists = (count > 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);

    return userExists;
}

bool DBManager::addUserIfNotExists(const std::string& telegramId, const std::string& username) {
    sqlite3* conn = threadPool.acquireConnection();

    if (isUserRegistered(telegramId)) {
        threadPool.releaseConnection(conn);
        return true;  // 用户已经存在
    }

    int rc = sqlite3_exec(conn, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to begin transaction.");
        threadPool.releaseConnection(conn);
        return false;
    }

    std::string insertSQL = "INSERT INTO users (telegram_id, username) VALUES (?, ?)";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(conn, insertSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement: " + std::string(sqlite3_errmsg(conn)));
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert user.");
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_exec(conn, "COMMIT;", nullptr, nullptr, nullptr);
    log(LogLevel::INFO, "New user inserted successfully.");
    threadPool.releaseConnection(conn);
    return true;
}

bool DBManager::addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName, const std::string& shortId, const std::string& shortLink,const std::string& extension) {
    sqlite3* conn = threadPool.acquireConnection();

    sqlite3_exec(conn, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // 1. 尝试插入文件记录，忽略冲突（如文件已存在）
    std::string insertFileSQL = "INSERT OR IGNORE INTO files (user_id, file_id, file_link, file_name, short_id, short_link, extension) "
                                "VALUES ((SELECT id FROM users WHERE telegram_id = ?), ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(conn, insertFileSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement (File): " + std::string(sqlite3_errmsg(conn)));
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fileLink.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fileName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, shortId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, shortLink.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, extension.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert file record: " + std::string(sqlite3_errmsg(conn)));
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        threadPool.releaseConnection(conn);
        return false;
    }

    // 2. 如果文件已经存在并且 extension 为空且传入的 extension 不为空，则更新 extension
    std::string updateExtensionSQL = "UPDATE files SET extension = ? "
                                     "WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?) "
                                     "AND file_id = ? AND (extension IS NULL OR extension = '')";
    sqlite3_stmt* updateStmt;
    rc = sqlite3_prepare_v2(conn, updateExtensionSQL.c_str(), -1, &updateStmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (extension): " + std::string(sqlite3_errmsg(conn)));
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_bind_text(updateStmt, 1, extension.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(updateStmt, 2, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(updateStmt, 3, fileId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(updateStmt);
    sqlite3_finalize(updateStmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to update extension: " + std::string(sqlite3_errmsg(conn)));
        sqlite3_exec(conn, "ROLLBACK;", nullptr, nullptr, nullptr);
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_exec(conn, "COMMIT;", nullptr, nullptr, nullptr);
    threadPool.releaseConnection(conn);
    log(LogLevel::INFO, "File record inserted successfully.");
    return true;
}

bool DBManager::removeFile(const std::string& userId, const std::string& fileName) {
    sqlite3* conn = threadPool.acquireConnection();

    std::string deleteSQL = "DELETE FROM files "
                            "WHERE id = ? AND user_id IN (SELECT id FROM users WHERE telegram_id = ?)";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(conn, deleteSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare DELETE statement (File): " + std::string(sqlite3_errmsg(conn)));
        threadPool.releaseConnection(conn);
        return false;
    }

    log(LogLevel::INFO, "Attempting to delete file with userId: " + userId + " and fileName: " + fileName);

    sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to delete file record: " + std::string(sqlite3_errmsg(conn)) + " Error code: " + std::to_string(rc));
        sqlite3_finalize(stmt);
        threadPool.releaseConnection(conn);
        return false;
    }

    int changes = sqlite3_changes(conn);
    if (changes == 0) {
        log(LogLevel::WARNING, "No records were deleted. Either the user or file was not found.");
        sqlite3_finalize(stmt);
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "File record deleted successfully.");
    threadPool.releaseConnection(conn);
    return true;
}

bool DBManager::banUser(const std::string& telegramId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string updateSQL = "UPDATE users SET is_banned = 1 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(conn, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Ban): " + std::string(sqlite3_errmsg(conn)));
        threadPool.releaseConnection(conn);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    threadPool.releaseConnection(conn);

    return rc == SQLITE_DONE;
}

bool DBManager::unbanUser(const std::string& telegramId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string updateSQL = "UPDATE users SET is_banned = 0 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(conn, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Unban): " + std::string(sqlite3_errmsg(conn)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    threadPool.releaseConnection(conn);

    return rc == SQLITE_DONE;
}

std::vector<std::tuple<std::string, std::string, std::string>> DBManager::getUserFiles(const std::string& userId, int page, int pageSize) {
    sqlite3* conn = threadPool.acquireConnection();
    std::vector<std::tuple<std::string, std::string, std::string>> files;
    std::string selectSQL = "SELECT file_name, file_link, id FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ? ORDER BY updated_at DESC) LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(conn, selectSQL.c_str(), -1, &stmt, nullptr);
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
    threadPool.releaseConnection(conn);
    return files;
}

int DBManager::getUserFileCount(const std::string& userId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string query = "SELECT COUNT(*) FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?)";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);

    return count;
}

std::string DBManager::getFileIdByShortId(const std::string& shortId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string query = "SELECT file_id FROM files WHERE short_id = ? LIMIT 1";  // 添加 LIMIT 1 只取第一条
    sqlite3_stmt* stmt;
    std::string fileId;  // 初始化为空字符串

    // 准备 SQL 语句
    if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);
    // 打印日志
    log(LogLevel::INFO, "Select file_id by short_id: " + shortId + ", file ID: " + fileId);
    return fileId;
}

void DBManager::setRegistrationOpen(bool isOpen) {
    sqlite3* conn = threadPool.acquireConnection();

    std::string updateSQL = "INSERT OR REPLACE INTO settings (key, value) VALUES ('registration', ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(conn, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, isOpen ? "1" : "0", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to update registration setting: " + std::string(sqlite3_errmsg(conn)));
    } else {
        log(LogLevel::INFO, "Registration setting updated successfully.");
    }
    threadPool.releaseConnection(conn);
}

bool DBManager::isRegistrationOpen() {
    sqlite3* conn = threadPool.acquireConnection();

    std::string selectSQL = "SELECT value FROM settings WHERE key = 'registration'";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(conn, selectSQL.c_str(), -1, &stmt, nullptr);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        bool isOpen = value == "1";
        log(LogLevel::INFO, "Registration is " + std::string(isOpen ? "open" : "closed") + ".");
        threadPool.releaseConnection(conn);
        return isOpen;
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Registration status not found, defaulting to open.");
    threadPool.releaseConnection(conn);
    return true;
}

int DBManager::getTotalUserCount() {
    sqlite3* conn = threadPool.acquireConnection();
    std::string query = "SELECT COUNT(*) FROM users";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);

    return count;
}

std::vector<std::tuple<std::string, std::string, bool>> DBManager::getUsersForBan(int page, int pageSize) {
    sqlite3* conn = threadPool.acquireConnection();
    std::vector<std::tuple<std::string, std::string, bool>> users;
    std::string selectSQL = "SELECT telegram_id, username, is_banned FROM users ORDER BY updated_at DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(conn, selectSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);

    return users;
}

bool DBManager::isUserBanned(const std::string& telegramId) {
    sqlite3* conn = threadPool.acquireConnection();
    std::string query = "SELECT is_banned FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool isBanned = false;

    if (sqlite3_prepare_v2(conn, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            isBanned = sqlite3_column_int(stmt, 0) == 1;
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);
    return isBanned;
}
std::vector<std::tuple<std::string, std::string, std::string, std::string>> DBManager::getImagesAndVideos(int page, int pageSize) {
    sqlite3* conn = threadPool.acquireConnection();
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
    if (sqlite3_prepare_v2(conn, selectSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(conn)));
    }
    threadPool.releaseConnection(conn);

    return files;
}