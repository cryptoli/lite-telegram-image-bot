#include "db_manager.h"
#include "utils.h"
#include <iostream>
#include <mutex>

// 全局锁，确保多线程访问时线程安全
std::mutex dbMutex;

sqlite3* DBManager::sharedDb = nullptr;

DBManager::DBManager(const std::string& dbFile) : dbFile(dbFile) {
    std::lock_guard<std::mutex> lock(dbMutex);

    if (!sharedDb) {
        if (sqlite3_open_v2(dbFile.c_str(), &sharedDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            log(LogLevel::LOGERROR, "Can't open database: " + std::string(sqlite3_errmsg(sharedDb)));
        } else {
            log(LogLevel::INFO, "Shared database opened successfully.");
        }
    }
}

DBManager::~DBManager() {
    // std::lock_guard<std::mutex> lock(dbMutex);
    if (sharedDb) {
        sqlite3_close(sharedDb);
        log(LogLevel::INFO, "Database connection closed.");
        sharedDb = nullptr;  // 确保指针清空，防止重复关闭
    }
}

bool DBManager::initialize() {
    // std::lock_guard<std::mutex> lock(dbMutex);

    if (!sharedDb) {
        log(LogLevel::LOGERROR, "Shared database is not initialized.");
        return false;
    }

    log(LogLevel::INFO, "Using database file: " + dbFile);

    // 打开数据库并设置一些配置
    int rc = sqlite3_exec(sharedDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to set journal mode to WAL: " + std::string(sqlite3_errmsg(sharedDb)));
        return false;
    }
    log(LogLevel::INFO, "Database setting ok!");

    return createTables();
}

bool DBManager::createTables() {
    // 创建用户表
    log(LogLevel::INFO, "Creating user table...");
    const char* userTableSQL = "CREATE TABLE IF NOT EXISTS users ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "telegram_id INTEGER UNIQUE, "
                               "username TEXT, "
                               "is_banned BOOLEAN DEFAULT 0, "
                               "created_at TEXT DEFAULT (datetime('now')), "
                               "updated_at TEXT DEFAULT (datetime('now')));";
    char* errMsg = 0;
    int rc = sqlite3_exec(sharedDb, userTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "User table created or exists already.");

    // 创建触发器，更新`updated_at`字段
    const char* userTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_user_timestamp "
                                      "AFTER UPDATE ON users "
                                      "FOR EACH ROW "
                                      "BEGIN "
                                      "UPDATE users SET updated_at = datetime('now') WHERE id = OLD.id; "
                                      "END;";
    rc = sqlite3_exec(sharedDb, userTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (User Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "User table trigger created or exists already.");

    // 创建文件表
    log(LogLevel::INFO, "Creating file table...");
    const char* fileTableSQL = "CREATE TABLE IF NOT EXISTS files ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "user_id INTEGER, "
                               "file_id TEXT, "
                               "file_link TEXT, "
                               "file_name TEXT, "
                               "is_valid BOOLEAN DEFAULT 1, "
                               "created_at TEXT DEFAULT (datetime('now')), "
                               "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(sharedDb, fileTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "File table created or exists already.");

    // 创建触发器，更新`updated_at`字段
    const char* fileTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_file_timestamp "
                                      "AFTER UPDATE ON files "
                                      "FOR EACH ROW "
                                      "BEGIN "
                                      "UPDATE files SET updated_at = datetime('now') WHERE id = OLD.id; "
                                      "END;";
    rc = sqlite3_exec(sharedDb, fileTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (File Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "File table trigger created or exists already.");

    // 创建设置表
    log(LogLevel::INFO, "Creating settings table...");
    const char* settingsTableSQL = "CREATE TABLE IF NOT EXISTS settings ("
                                   "key TEXT PRIMARY KEY, "
                                   "value TEXT, "
                                   "created_at TEXT DEFAULT (datetime('now')), "
                                   "updated_at TEXT DEFAULT (datetime('now')));";
    rc = sqlite3_exec(sharedDb, settingsTableSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "Settings table created or exists already.");

    // 创建触发器，更新`updated_at`字段
    const char* settingsTableTriggerSQL = "CREATE TRIGGER IF NOT EXISTS update_settings_timestamp "
                                          "AFTER UPDATE ON settings "
                                          "FOR EACH ROW "
                                          "BEGIN "
                                          "UPDATE settings SET updated_at = datetime('now') WHERE key = OLD.key; "
                                          "END;";
    rc = sqlite3_exec(sharedDb, settingsTableTriggerSQL, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "SQL error (Settings Table Trigger): " + std::string(errMsg));
        sqlite3_free(errMsg);
        return false;
    }
    log(LogLevel::INFO, "Settings table trigger created or exists already.");

    return true;
}

bool DBManager::isUserRegistered(const std::string& telegramId) {
    // std::lock_guard<std::mutex> lock(dbMutex);

    std::string query = "SELECT COUNT(*) FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool userExists = false;

    if (sqlite3_prepare_v2(sharedDb, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            userExists = (count > 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
    }

    return userExists;
}

bool DBManager::addUserIfNotExists(const std::string& telegramId, const std::string& username) {
    // std::lock_guard<std::mutex> lock(dbMutex);

    if (isUserRegistered(telegramId)) {
        return true;  // 用户已经存在
    }

    int rc = sqlite3_exec(sharedDb, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to begin transaction.");
        return false;
    }

    std::string insertSQL = "INSERT INTO users (telegram_id, username) VALUES (?, ?)";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(sharedDb, insertSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement: " + std::string(sqlite3_errmsg(sharedDb)));
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert user.");
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_exec(sharedDb, "COMMIT;", nullptr, nullptr, nullptr);
    log(LogLevel::INFO, "New user inserted successfully.");
    return true;
}

bool DBManager::addFile(const std::string& userId, const std::string& fileId, const std::string& fileLink, const std::string& fileName) {
    // std::lock_guard<std::mutex> lock(dbMutex);

    sqlite3_exec(sharedDb, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // 1. 首先检查是否已经存在相同的 user_id 和 file_id
    std::string checkFileSQL = "SELECT COUNT(*) FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?) AND file_id = ?";
    sqlite3_stmt* checkStmt;
    int rc = sqlite3_prepare_v2(sharedDb, checkFileSQL.c_str(), -1, &checkStmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_text(checkStmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(checkStmt, 2, fileId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(checkStmt);
    int fileCount = sqlite3_column_int(checkStmt, 0);
    sqlite3_finalize(checkStmt);

    // 2. 如果文件已经存在，取消插入并返回
    if (fileCount > 0) {
        log(LogLevel::INFO, "File with ID: " + fileId + " already exists for user: " + userId);
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    // 3. 插入文件记录
    std::string insertFileSQL = "INSERT INTO files (user_id, file_id, file_link, file_name) "
                                "VALUES ((SELECT id FROM users WHERE telegram_id = ?), ?, ?, ?)";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(sharedDb, insertFileSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare INSERT statement (File): " + std::string(sqlite3_errmsg(sharedDb)));
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fileId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, fileLink.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fileName.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to insert file record: " + std::string(sqlite3_errmsg(sharedDb)));
        sqlite3_exec(sharedDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_exec(sharedDb, "COMMIT;", nullptr, nullptr, nullptr);
    log(LogLevel::INFO, "File record inserted successfully.");
    return true;
}

bool DBManager::removeFile(const std::string& userId, const std::string& fileName) {
    // std::lock_guard<std::mutex> lock(dbMutex);

    std::string deleteSQL = "DELETE FROM files "
                            "WHERE id = ? AND user_id IN (SELECT id FROM users WHERE telegram_id = ?)";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(sharedDb, deleteSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare DELETE statement (File): " + std::string(sqlite3_errmsg(sharedDb)));
        return false;
    }

    log(LogLevel::INFO, "Attempting to delete file with userId: " + userId + " and fileName: " + fileName);

    sqlite3_bind_text(stmt, 1, fileName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to delete file record: " + std::string(sqlite3_errmsg(sharedDb)) + " Error code: " + std::to_string(rc));
        sqlite3_finalize(stmt);
        return false;
    }

    int changes = sqlite3_changes(sharedDb);
    if (changes == 0) {
        log(LogLevel::WARNING, "No records were deleted. Either the user or file was not found.");
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "File record deleted successfully.");
    return true;
}

bool DBManager::banUser(const std::string& telegramId) {
    std::string updateSQL = "UPDATE users SET is_banned = 1 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(sharedDb, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Ban): " + std::string(sqlite3_errmsg(sharedDb)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool DBManager::unbanUser(const std::string& telegramId) {
    std::string updateSQL = "UPDATE users SET is_banned = 0 WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(sharedDb, updateSQL.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        log(LogLevel::LOGERROR, "Failed to prepare UPDATE statement (User Unban): " + std::string(sqlite3_errmsg(sharedDb)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<std::tuple<std::string, std::string, std::string>> DBManager::getUserFiles(const std::string& userId, int page, int pageSize) {
    std::vector<std::tuple<std::string, std::string, std::string>> files;
    std::string selectSQL = "SELECT file_name, file_link, id FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ? ORDER BY updated_at DESC) LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(sharedDb, selectSQL.c_str(), -1, &stmt, nullptr);
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
    return files;
}

int DBManager::getUserFileCount(const std::string& userId) {
    std::string query = "SELECT COUNT(*) FROM files WHERE user_id = (SELECT id FROM users WHERE telegram_id = ?)";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(sharedDb, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, userId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
    }

    return count;
}


void DBManager::setRegistrationOpen(bool isOpen) {
    // std::lock_guard<std::mutex> lock(dbMutex);

    std::string updateSQL = "INSERT OR REPLACE INTO settings (key, value) VALUES ('registration', ?)";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(sharedDb, updateSQL.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, isOpen ? "1" : "0", -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        log(LogLevel::LOGERROR, "Failed to update registration setting: " + std::string(sqlite3_errmsg(sharedDb)));
    } else {
        log(LogLevel::INFO, "Registration setting updated successfully.");
    }
}

bool DBManager::isRegistrationOpen() {
    // std::lock_guard<std::mutex> lock(dbMutex);

    std::string selectSQL = "SELECT value FROM settings WHERE key = 'registration'";
    sqlite3_stmt* stmt;
    
    sqlite3_prepare_v2(sharedDb, selectSQL.c_str(), -1, &stmt, nullptr);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        bool isOpen = value == "1";
        log(LogLevel::INFO, "Registration is " + std::string(isOpen ? "open" : "closed") + ".");
        return isOpen;
    }
    sqlite3_finalize(stmt);
    log(LogLevel::INFO, "Registration status not found, defaulting to open.");
    return true;
}

int DBManager::getTotalUserCount() {
    std::string query = "SELECT COUNT(*) FROM users";
    sqlite3_stmt* stmt;
    int count = 0;

    if (sqlite3_prepare_v2(sharedDb, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
    }

    return count;
}

std::vector<std::tuple<std::string, std::string, bool>> DBManager::getUsersForBan(int page, int pageSize) {
    std::vector<std::tuple<std::string, std::string, bool>> users;
    std::string selectSQL = "SELECT telegram_id, username, is_banned FROM users ORDER BY updated_at DESC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(sharedDb, selectSQL.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
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
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
    }

    return users;
}

bool DBManager::isUserBanned(const std::string& telegramId) {
    std::string query = "SELECT is_banned FROM users WHERE telegram_id = ?";
    sqlite3_stmt* stmt;
    bool isBanned = false;

    if (sqlite3_prepare_v2(sharedDb, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, telegramId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            isBanned = sqlite3_column_int(stmt, 0) == 1;
        }
        sqlite3_finalize(stmt);
    } else {
        log(LogLevel::LOGERROR, "Failed to prepare SELECT statement: " + std::string(sqlite3_errmsg(sharedDb)));
    }

    return isBanned;
}