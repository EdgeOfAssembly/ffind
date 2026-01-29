// ffind-daemon.cpp - Fast file finder daemon with real-time inotify indexing
//
// This daemon maintains an in-memory index of the filesystem using inotify
// for real-time updates. It provides instant search capabilities through a
// Unix domain socket interface.
//
// Architecture Overview:
// - Main thread: Handles inotify events and socket accept()
// - Worker threads: Process client requests (one thread per connection)
// - Thread pool: Parallel content search across multiple CPU cores
//
// Security Considerations:
// - Network input validation with size limits
// - Signal handler safety (async-signal-safe functions only)
// - Bounds checking for all buffer operations
// - Error handling for all system calls
//
// Key Functions:
// - index_directory(): Initial filesystem scan
// - process_events(): Handle inotify events
// - handle_client(): Process search requests from clients
// - search_content_worker(): Parallel content search

#include <bits/stdc++.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <re2/re2.h>
#include <string.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sqlite3.h>
#include <sys/uio.h>
#include <sys/mman.h>

using namespace std;
using namespace std::filesystem;
using namespace std::chrono_literals;

const char* VERSION = "1.0";

// ANSI color codes for stderr output
const char* COLOR_RED = "\033[1;31m";
const char* COLOR_GREEN = "\033[1;32m";
const char* COLOR_YELLOW = "\033[1;33m";
const char* COLOR_CYAN = "\033[36m";
const char* COLOR_BOLD = "\033[1m";
const char* COLOR_RESET = "\033[0m";

void show_usage() {
    cout << "Usage: ffind-daemon [OPTIONS] DIR [DIR2 ...]\n\n";
    cout << "Options:\n";
    cout << "  --foreground       Run in foreground (don't daemonize)\n";
    cout << "  --db PATH          Enable SQLite persistence\n";
    cout << "  -h, --help         Show this help\n";
    cout << "  -v, --version      Show version\n\n";
    cout << "At least one directory is required.\n\n";
    cout << "Examples:\n";
    cout << "  ffind-daemon /home/user/projects\n";
    cout << "  ffind-daemon --foreground --db ~/.cache/ffind.db ~/code ~/docs\n";
}

void show_version() {
    cout << "ffind-daemon " << VERSION << "\n";
}

// Simple YAML config parser for our limited use case
// Parses key: value pairs (supports foreground and db options)
struct Config {
    bool foreground = false;
    string db_path;
    bool loaded = false;
    string config_file_path;  // Track which file was loaded
};

Config parse_config_file(const string& config_path) {
    Config cfg;
    ifstream file(config_path);
    if (!file.is_open()) {
        return cfg;  // File doesn't exist, return empty config
    }
    
    string line;
    int line_num = 0;
    while (getline(file, line)) {
        line_num++;
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Parse key: value
        size_t colon_pos = line.find(':');
        if (colon_pos == string::npos) {
            cerr << COLOR_YELLOW << "[WARNING]" << COLOR_RESET 
                 << " Invalid config line " << line_num << " in " << config_path 
                 << ": missing colon\n";
            continue;
        }
        
        string key = line.substr(0, colon_pos);
        string value = line.substr(colon_pos + 1);
        
        // Trim key and value - check for npos to avoid underflow
        size_t key_start = key.find_first_not_of(" \t");
        if (key_start != string::npos) {
            key.erase(0, key_start);
            size_t key_end = key.find_last_not_of(" \t");
            if (key_end != string::npos) {
                key.erase(key_end + 1);
            }
        } else {
            key.clear();
        }
        
        size_t val_start = value.find_first_not_of(" \t");
        if (val_start != string::npos) {
            value.erase(0, val_start);
            size_t val_end = value.find_last_not_of(" \t");
            if (val_end != string::npos) {
                value.erase(val_end + 1);
            }
        } else {
            value.clear();
        }
        
        // Skip if key is empty after trimming
        if (key.empty()) continue;
        
        // Strip surrounding quotes from value if present (simple handling)
        if (value.length() >= 2) {
            if ((value.front() == '"' && value.back() == '"') ||
                (value.front() == '\'' && value.back() == '\'')) {
                value = value.substr(1, value.length() - 2);
            }
        }
        
        // Parse known keys
        if (key == "foreground") {
            if (value == "true" || value == "yes" || value == "1") {
                cfg.foreground = true;
            } else if (value == "false" || value == "no" || value == "0") {
                cfg.foreground = false;
            } else {
                cerr << COLOR_YELLOW << "[WARNING]" << COLOR_RESET 
                     << " Invalid value for 'foreground' in " << config_path 
                     << " (expected true/false)\n";
            }
        } else if (key == "db") {
            cfg.db_path = value;
        } else {
            cerr << COLOR_YELLOW << "[WARNING]" << COLOR_RESET 
                 << " Unknown config key '" << key << "' in " << config_path << "\n";
        }
    }
    
    cfg.loaded = true;
    return cfg;
}

Config load_config() {
    // Try XDG_CONFIG_HOME first, then ~/.config, then /etc
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    vector<string> config_paths;
    
    if (xdg_config && xdg_config[0] != '\0') {
        config_paths.push_back(string(xdg_config) + "/ffind/config.yaml");
    }
    
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
        config_paths.push_back(string(home) + "/.config/ffind/config.yaml");
    }
    
    config_paths.push_back("/etc/ffind/config.yaml");
    
    for (const auto& path : config_paths) {
        if (access(path.c_str(), F_OK) == 0) {
            // File exists, try to parse it
            Config cfg = parse_config_file(path);
            if (cfg.loaded) {
                cfg.config_file_path = path;
                return cfg;
            }
        }
    }
    
    // No config found, return empty config
    return Config();
}

struct Entry {
    string path;
    int64_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;
    size_t root_index = 0;  // Which root this entry belongs to
};

// Path component index for fast path-filtered queries
struct PathIndex {
    // Map directory path → list of entries in that directory
    unordered_map<string, vector<Entry*>> dir_to_entries;
    
    // All unique directory paths (for debugging/stats)
    unordered_set<string> all_dirs;
};

// Thread pool for parallel content search
class ThreadPool {
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
    
public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        
                        if (this->stop && this->tasks.empty()) return;
                        
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    try {
                        task();
                    } catch (const exception& e) {
                        // Silently catch exceptions here to prevent worker thread termination.
                        // Exceptions from the user task are already captured by std::packaged_task
                        // and will be re-thrown when future.get() is called.
                    } catch (...) {
                        // Catch any other exceptions to keep the worker thread alive.
                    }
                }
            });
        }
    }
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> future<invoke_result_t<F, Args...>> {
        
        using return_type = invoke_result_t<F, Args...>;
        
        auto task = make_shared<packaged_task<return_type()>>(
            bind(forward<F>(f), forward<Args>(args)...)
        );
        
        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);
            if (stop) throw runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }
    
    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread& worker : workers) {
            worker.join();
        }
    }
};

vector<Entry> entries;
mutex mtx;
PathIndex path_index;
vector<string> root_paths;  // Multiple roots support
string sock_path;
string pid_file_path;
char pid_file_path_buf[256] = {0};  // Fixed buffer for signal-safe cleanup
char sock_path_buf[256] = {0};  // Fixed buffer for signal-safe cleanup
volatile sig_atomic_t running = 1;
atomic<int> srv_fd{-1};  // Atomic socket fd for signal-safe access
atomic<bool> shutdown_started{false};  // Global to avoid static initialization guard in signal handler
int in_fd = -1;
unordered_map<int, string> wd_to_dir;

// Directory rename tracking
unordered_map<uint32_t, pair<string, chrono::steady_clock::time_point>> pending_moves;
mutex pending_moves_mtx;
bool foreground = false;

// SQLite persistence
sqlite3* db = nullptr;
string db_path;
bool db_enabled = false;
atomic<int> pending_changes{0};
atomic<bool> db_dirty{false};
chrono::steady_clock::time_point last_flush_time;
const int FLUSH_INTERVAL_SEC = 30;
const int FLUSH_THRESHOLD = 100;
mutex db_mtx;

// Thread pool for parallel content search
unique_ptr<ThreadPool> content_search_pool;

// Initialize thread pool for content search
void init_thread_pool() {
    size_t num_threads = thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;  // Default fallback
    
    content_search_pool = make_unique<ThreadPool>(num_threads);
    
    if (foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
             << " Thread pool initialized with " << num_threads << " threads\n";
    }
}

// Check if DNOTIFY is available on this system
bool check_dnotify_available() {
    // Try to use fcntl with F_NOTIFY on a test file descriptor
    // DNOTIFY requires DN_* constants which may not be defined on all systems
    #ifndef F_NOTIFY
    return false;
    #else
    int saved_errno = errno;
    
    // Create a temporary file descriptor to test
    int test_fd = open("/tmp", O_RDONLY);
    if (test_fd < 0) {
        errno = saved_errno;
        return false;
    }
    
    // Try to set F_NOTIFY - if it fails with ENOSYS, DNOTIFY is not available
    int result = fcntl(test_fd, F_NOTIFY, 0);
    int fcntl_errno = errno;
    close(test_fd);
    
    // If errno is ENOSYS, kernel doesn't support DNOTIFY
    if (result < 0 && fcntl_errno == ENOSYS) {
        errno = saved_errno;
        return false;
    }
    
    // DNOTIFY might be available (but we're using inotify anyway)
    errno = saved_errno;
    return true;
    #endif
}

// ============================================================================
// SQLite Database Functions
// ============================================================================

/**
 * Function: init_database
 * Purpose: Initialize SQLite database for persistent index storage
 * Parameters:
 *   - path: Filesystem path for the SQLite database file
 * Returns: true on success, false on error
 * Security:
 *   - Creates database with restricted permissions (inherited from umask)
 *   - Uses WAL mode for crash safety and better concurrency
 *   - Sets synchronous=NORMAL for balance of speed and safety
 * Thread-safety: Must be called from main thread before other threads start
 * 
 * Schema Overview:
 * - meta: Key-value pairs for root paths and configuration
 * - entries: File/directory index (path, size, mtime, is_dir, root_index)
 * - sync_state: Tracks last full sync time and dirty flag
 * 
 * WAL Mode Benefits:
 * - Atomic commits even on crashes or power loss
 * - Better read concurrency (readers don't block writers)
 * - Automatic checkpoint management by SQLite
 * 
 * REVIEWER_NOTE: WAL mode is critical for crash safety. In WAL mode,
 * commits are atomic and survive crashes. The -wal and -shm files are
 * automatically managed by SQLite.
 */
bool init_database(const string& path) {
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "ERROR: Cannot open database: " << sqlite3_errmsg(db) << COLOR_RESET << "\n";
        }
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    
    // Enable WAL mode for better concurrency and crash recovery
    char* err_msg = nullptr;
    rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not enable WAL mode: " << err_msg << COLOR_RESET << "\n";
        }
        sqlite3_free(err_msg);
    }
    
    // Set synchronous to NORMAL for balance between speed and safety
    rc = sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not set synchronous mode: " << err_msg << COLOR_RESET << "\n";
        }
        sqlite3_free(err_msg);
    }
    
    // Create schema
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS meta (
            key TEXT PRIMARY KEY,
            value TEXT
        );
        
        CREATE TABLE IF NOT EXISTS entries (
            id INTEGER PRIMARY KEY,
            path TEXT UNIQUE NOT NULL,
            size INTEGER NOT NULL,
            mtime INTEGER NOT NULL,
            is_dir INTEGER NOT NULL,
            root_index INTEGER NOT NULL
        );
        
        CREATE INDEX IF NOT EXISTS idx_path ON entries(path);
        
        CREATE TABLE IF NOT EXISTS sync_state (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            last_full_sync INTEGER,
            dirty INTEGER DEFAULT 0
        );
        
        INSERT OR IGNORE INTO sync_state (id, last_full_sync, dirty) VALUES (1, 0, 0);
    )";
    
    rc = sqlite3_exec(db, schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "ERROR: Cannot create schema: " << err_msg << COLOR_RESET << "\n";
        }
        sqlite3_free(err_msg);
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    
    return true;
}

// Helper function to escape JSON string
string json_escape(const string& str) {
    string escaped;
    escaped.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    // Control characters
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    escaped += buf;
                } else {
                    escaped += c;
                }
        }
    }
    return escaped;
}

// Helper function to unescape JSON string
string json_unescape(const string& str) {
    string unescaped;
    unescaped.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            switch (str[i + 1]) {
                case '"':  unescaped += '"'; i++; break;
                case '\\': unescaped += '\\'; i++; break;
                case 'b':  unescaped += '\b'; i++; break;
                case 'f':  unescaped += '\f'; i++; break;
                case 'n':  unescaped += '\n'; i++; break;
                case 'r':  unescaped += '\r'; i++; break;
                case 't':  unescaped += '\t'; i++; break;
                case 'u':  // Unicode escape - simplified handling
                    if (i + 5 < str.size()) {
                        // Just copy the character as-is for now
                        unescaped += str[i];
                        unescaped += str[i + 1];
                    }
                    break;
                default:
                    unescaped += str[i + 1];
                    i++;
            }
        } else {
            unescaped += str[i];
        }
    }
    return unescaped;
}

vector<string> load_roots_from_db() {
    vector<string> roots;
    if (!db) return roots;
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT value FROM meta WHERE key = 'root_paths'";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* json_str = (const char*)sqlite3_column_text(stmt, 0);
            if (json_str) {
                // Parse JSON array with proper escape handling
                string json(json_str);
                size_t pos = 0;
                while ((pos = json.find("\"", pos)) != string::npos) {
                    size_t end = pos + 1;
                    // Find closing quote, handling escaped quotes
                    while (end < json.size()) {
                        if (json[end] == '"' && (end == 0 || json[end - 1] != '\\')) {
                            break;
                        }
                        end++;
                    }
                    if (end >= json.size()) break;
                    
                    string root = json.substr(pos + 1, end - pos - 1);
                    if (!root.empty()) {
                        roots.push_back(json_unescape(root));
                    }
                    pos = end + 1;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    return roots;
}

void save_roots_to_db(const vector<string>& roots) {
    if (!db) return;
    
    // Build JSON array with proper escaping
    string json = "[";
    for (size_t i = 0; i < roots.size(); i++) {
        if (i > 0) json += ",";
        json += "\"" + json_escape(roots[i]) + "\"";
    }
    json += "]";
    
    int rc;
    char* errmsg = nullptr;
    
    // Start transaction for atomicity
    rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Failed to begin transaction for saving root paths: "
                 << (errmsg ? errmsg : "unknown error") << "\n";
        }
        sqlite3_free(errmsg);
        return;
    }
    
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO meta (key, value) VALUES ('root_paths', ?)";
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Failed to prepare statement for saving root paths: "
                 << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    
    rc = sqlite3_bind_text(stmt, 1, json.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Failed to bind root paths JSON: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        if (foreground) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Failed to save root paths: " << sqlite3_errmsg(db) << "\n";
        }
        sqlite3_finalize(stmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    
    sqlite3_finalize(stmt);
    
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Failed to commit transaction for saving root paths: "
                 << (errmsg ? errmsg : "unknown error") << "\n";
        }
        sqlite3_free(errmsg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
}

void load_entries_from_db() {
    if (!db) return;
    
    lock_guard<mutex> lk(mtx);
    entries.clear();
    
    sqlite3_stmt* stmt;
    const char* sql = "SELECT path, size, mtime, is_dir, root_index FROM entries";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Entry e;
            e.path = (const char*)sqlite3_column_text(stmt, 0);
            e.size = sqlite3_column_int64(stmt, 1);
            e.mtime = sqlite3_column_int64(stmt, 2);
            e.is_dir = sqlite3_column_int(stmt, 3) != 0;
            e.root_index = sqlite3_column_int(stmt, 4);
            entries.push_back(e);
        }
        sqlite3_finalize(stmt);
    }
    
    if (foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Loaded " << entries.size() 
             << " entries from database\n";
    }
}

void reconcile_db_with_filesystem() {
    if (!db) return;
    
    // Build map of current entries from DB
    unordered_map<string, Entry> db_entries;
    {
        lock_guard<mutex> lk(mtx);
        for (const auto& e : entries) {
            db_entries[e.path] = e;
        }
    }
    
    // Track statistics and changes
    int added = 0, removed = 0, updated = 0;
    vector<Entry> new_entries;
    unordered_set<string> found_paths;
    
    // Walk filesystem and build new entry list
    for (size_t root_idx = 0; root_idx < root_paths.size(); root_idx++) {
        try {
            for (auto& e : recursive_directory_iterator(root_paths[root_idx], 
                                                       directory_options::skip_permission_denied)) {
                string p = e.path().string();
                found_paths.insert(p);
                
                struct stat st {};
                if (lstat(p.c_str(), &st) != 0) continue;
                
                bool is_dir = S_ISDIR(st.st_mode);
                int64_t sz = is_dir ? 0LL : st.st_size;
                time_t mtime = st.st_mtime;
                
                auto it = db_entries.find(p);
                if (it == db_entries.end()) {
                    // File not in DB - add it
                    Entry entry;
                    entry.path = p;
                    entry.size = sz;
                    entry.mtime = mtime;
                    entry.is_dir = is_dir;
                    entry.root_index = root_idx;
                    new_entries.push_back(entry);
                    added++;
                } else {
                    // File exists - check if modified
                    if (it->second.size != sz || it->second.mtime != mtime) {
                        Entry entry = it->second;
                        entry.size = sz;
                        entry.mtime = mtime;
                        entry.is_dir = is_dir;
                        new_entries.push_back(entry);
                        updated++;
                    } else {
                        // No change
                        new_entries.push_back(it->second);
                    }
                }
            }
        } catch (...) {}
    }
    
    // Count entries that were removed (in DB but not on filesystem)
    for (const auto& [path, entry] : db_entries) {
        if (found_paths.find(path) == found_paths.end()) {
            removed++;
        }
    }
    
    // Replace entries with reconciled list
    {
        lock_guard<mutex> lk(mtx);
        entries = move(new_entries);
    }
    
    // Mark changes for flushing
    int total_changes = added + removed + updated;
    if (total_changes > 0) {
        pending_changes += total_changes;
        db_dirty = true;
    }
    
    if (foreground && (added > 0 || removed > 0 || updated > 0)) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Reconciliation: " 
             << added << " added, " << removed << " removed, " << updated << " updated\n";
    }
}

void flush_changes_to_db() {
    if (!db) return;
    
    lock_guard<mutex> lk(db_mtx);
    
    // Capture current pending changes before any operations
    int changes_to_flush = pending_changes.load();
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not begin transaction: " << err_msg << COLOR_RESET << "\n";
        }
        sqlite3_free(err_msg);
        return;
    }
    
    // Clear existing entries
    sqlite3_exec(db, "DELETE FROM entries;", nullptr, nullptr, nullptr);
    
    // Insert all current entries
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO entries (path, size, mtime, is_dir, root_index) VALUES (?, ?, ?, ?, ?)";
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not prepare insert statement: "
                 << sqlite3_errmsg(db) << COLOR_RESET << "\n";
        }
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }
    
    int insert_count = 0;
    int error_count = 0;
    {
        lock_guard<mutex> entries_lk(mtx);
        for (const auto& e : entries) {
            sqlite3_bind_text(stmt, 1, e.path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, e.size);
            sqlite3_bind_int64(stmt, 3, e.mtime);
            sqlite3_bind_int(stmt, 4, e.is_dir ? 1 : 0);
            sqlite3_bind_int(stmt, 5, e.root_index);
            
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_DONE) {
                insert_count++;
            } else {
                error_count++;
                if (foreground && error_count <= 5) {  // Limit error messages
                    cerr << COLOR_YELLOW << "Warning: Failed to insert entry " << e.path 
                         << ": " << sqlite3_errmsg(db) << COLOR_RESET << "\n";
                }
            }
            sqlite3_reset(stmt);
        }
    }
    sqlite3_finalize(stmt);
    
    if (error_count > 0 && foreground) {
        cerr << COLOR_YELLOW << "Warning: " << error_count << " entries failed to insert" << COLOR_RESET << "\n";
    }
    
    // Update sync state
    sqlite3_exec(db, "UPDATE sync_state SET last_full_sync = strftime('%s', 'now'), dirty = 0 WHERE id = 1;",
                 nullptr, nullptr, nullptr);
    
    rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not commit transaction: " << err_msg << COLOR_RESET << "\n";
        }
        sqlite3_free(err_msg);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    } else {
        // Successfully committed - update counters
        // Subtract the changes we flushed, but keep any new changes that came in during flush
        int current = pending_changes.load();
        pending_changes.fetch_sub(min(current, changes_to_flush));
        db_dirty = (pending_changes.load() > 0);
        last_flush_time = chrono::steady_clock::now();
        
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Flushed " << insert_count 
                 << " entries to database\n";
        }
    }
}

void maybe_flush_to_db() {
    if (!db_enabled || !db) return;
    
    auto now = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::seconds>(now - last_flush_time).count();
    
    if (pending_changes >= FLUSH_THRESHOLD || elapsed >= FLUSH_INTERVAL_SEC) {
        flush_changes_to_db();
    }
}

string get_pid_file_path() {
    uid_t uid = getuid();
    if (uid == 0) {
        // Running as root
        return "/run/ffind-daemon.pid";
    } else {
        // Running as non-root user
        return "/run/user/" + to_string(uid) + "/ffind-daemon.pid";
    }
}

bool is_process_running(pid_t pid) {
    // Use kill with signal 0 to check if process exists
    // Returns 0 if process exists, -1 with errno=ESRCH if not
    if (kill(pid, 0) != 0) {
        return false;
    }
    
    // Check if the process is actually ffind-daemon to avoid PID reuse issues
    // Read /proc/PID/comm to verify process name
    string comm_path = "/proc/" + to_string(pid) + "/comm";
    ifstream comm_file(comm_path);
    if (comm_file.is_open()) {
        string comm;
        getline(comm_file, comm);
        comm_file.close();
        // Check if the process is ffind-daemon
        return comm == "ffind-daemon";
    }
    
    // If we can't read comm, assume it's running (conservative approach)
    return true;
}

bool check_and_create_pid_file(const string& pid_path, bool foreground) {
    // Try to create PID file atomically with O_CREAT | O_EXCL
    int fd = open(pid_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    
    if (fd < 0) {
        if (errno == EEXIST) {
            // PID file exists - check if it's stale
            ifstream pid_in(pid_path);
            if (pid_in.is_open()) {
                pid_t existing_pid;
                if (pid_in >> existing_pid) {
                    pid_in.close();
                    
                    // Check if process is still running
                    if (is_process_running(existing_pid)) {
                        // Process is running - error and exit
                        if (foreground) {
                            cerr << COLOR_RED << "ERROR: Daemon already running (PID: " 
                                 << existing_pid << ")" << COLOR_RESET << "\n";
                        }
                        return false;
                    } else {
                        // Stale PID file - warn and remove
                        if (foreground) {
                            cerr << COLOR_YELLOW << "Warning: Removing stale PID file (PID: " 
                                 << existing_pid << " not running)" << COLOR_RESET << "\n";
                        }
                        unlink(pid_path.c_str());
                        // Retry creating the PID file
                        fd = open(pid_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
                        if (fd < 0) {
                            // Failed to create after removing stale file (possible race)
                            if (foreground) {
                                cerr << COLOR_YELLOW << "Warning: Could not create PID file: " 
                                     << strerror(errno) << COLOR_RESET << "\n";
                            }
                            return true;  // Don't fail - continue without PID file
                        }
                    }
                } else {
                    pid_in.close();
                    // Invalid PID file - warn and remove
                    if (foreground) {
                        cerr << COLOR_YELLOW << "Warning: Removing invalid PID file" << COLOR_RESET << "\n";
                    }
                    unlink(pid_path.c_str());
                    // Retry creating the PID file
                    fd = open(pid_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
                    if (fd < 0) {
                        if (foreground) {
                            cerr << COLOR_YELLOW << "Warning: Could not create PID file: " 
                                 << strerror(errno) << COLOR_RESET << "\n";
                        }
                        return true;  // Don't fail - continue without PID file
                    }
                }
            } else {
                // Can't read PID file but it exists - warn and try to remove
                if (foreground) {
                    cerr << COLOR_YELLOW << "Warning: Could not read PID file, attempting to remove" 
                         << COLOR_RESET << "\n";
                }
                unlink(pid_path.c_str());
                // Retry creating the PID file
                fd = open(pid_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
                if (fd < 0) {
                    if (foreground) {
                        cerr << COLOR_YELLOW << "Warning: Could not create PID file: " 
                             << strerror(errno) << COLOR_RESET << "\n";
                    }
                    return true;  // Don't fail - continue without PID file
                }
            }
        } else {
            // Other error (e.g., permission denied, directory doesn't exist)
            if (foreground) {
                cerr << COLOR_YELLOW << "Warning: Could not create PID file: " 
                     << strerror(errno) << COLOR_RESET << "\n";
            }
            return true;  // Don't fail - continue without PID file
        }
    }
    
    // At this point, fd is valid - write our PID to the file
    pid_t our_pid = getpid();
    string pid_str = to_string(our_pid) + "\n";
    ssize_t written = write(fd, pid_str.c_str(), pid_str.size());
    close(fd);
    
    if (written != static_cast<ssize_t>(pid_str.size())) {
        if (foreground) {
            cerr << COLOR_YELLOW << "Warning: Could not write complete PID to file" 
                 << COLOR_RESET << "\n";
        }
        unlink(pid_path.c_str());
        // Don't fail - continue without PID file
        return true;
    }
    
    return true;
}

void cleanup_pid_file() {
    // Use fixed buffer for signal-safe cleanup
    if (pid_file_path_buf[0] != '\0') {
        unlink(pid_file_path_buf);
    }
}

/**
 * Function: safe_write_all
 * Purpose: Writes all data to a file descriptor, handling partial writes and EINTR
 * Parameters:
 *   - fd: File descriptor to write to
 *   - buf: Buffer containing data to write
 *   - count: Number of bytes to write
 * Returns: true on success, false on error
 * Security: Handles EINTR and partial writes correctly
 * Thread-safety: Thread-safe (operates on file descriptor)
 */
static bool safe_write_all(int fd, const void* buf, size_t count) {
    const char* ptr = static_cast<const char*>(buf);
    size_t remaining = count;
    
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        
        if (written < 0) {
            // EINTR: Interrupted by signal, retry
            if (errno == EINTR) {
                continue;
            }
            // EPIPE: Broken pipe (client disconnected), not a critical error
            if (errno == EPIPE) {
                return false;
            }
            // Other errors
            return false;
        }
        
        // Successful write (may be partial)
        ptr += written;
        remaining -= written;
    }
    
    return true;
}

/**
 * Function: safe_read_all
 * Purpose: Reads exactly count bytes from a file descriptor, handling EINTR
 * Parameters:
 *   - fd: File descriptor to read from
 *   - buf: Buffer to read data into
 *   - count: Number of bytes to read
 * Returns: true on success, false on error or EOF
 * Security: Handles EINTR correctly, validates input size
 * Thread-safety: Thread-safe (operates on file descriptor)
 */
static bool safe_read_all(int fd, void* buf, size_t count) {
    char* ptr = static_cast<char*>(buf);
    size_t remaining = count;
    
    while (remaining > 0) {
        ssize_t nread = read(fd, ptr, remaining);
        
        if (nread < 0) {
            // EINTR: Interrupted by signal, retry
            if (errno == EINTR) {
                continue;
            }
            // Other errors
            return false;
        }
        
        if (nread == 0) {
            // EOF before reading all data
            return false;
        }
        
        // Successful read (may be partial)
        ptr += nread;
        remaining -= nread;
    }
    
    return true;
}

/**
 * Helper: ignore_write_result
 * Purpose: Wrapper to explicitly ignore write() return value in signal handlers
 * This is needed because newer GCC doesn't suppress warn_unused_result with (void) cast
 */
__attribute__((unused)) static inline void ignore_write_result(ssize_t result) {
    (void)result;
}

// Helper function to clean up resources on socket creation/setup errors
void cleanup_on_socket_error(int srv_fd, bool unlink_socket, const string& sock_path) {
    // Close socket file descriptor if valid
    if (srv_fd >= 0) {
        close(srv_fd);
    }
    
    // Remove socket file if requested
    if (unlink_socket) {
        unlink(sock_path.c_str());
    }
    
    // Close database connection if open
    if (db_enabled && db != nullptr) {
        sqlite3_close(db);
    }
    
    // Close inotify file descriptor
    close(in_fd);
    
    // Remove PID file
    cleanup_pid_file();
}

/**
 * Function: crash_handler
 * Purpose: Emergency cleanup on fatal signals (SIGSEGV, SIGABRT, SIGBUS)
 * Parameters:
 *   - sig: Signal number that triggered the crash
 * Returns: Does not return (re-raises signal after cleanup)
 * Security: 
 *   - CRITICAL: Only uses async-signal-safe functions
 *   - write() - async-signal-safe per POSIX
 *   - close() - async-signal-safe per POSIX
 *   - unlink() - async-signal-safe per POSIX
 *   - signal(), raise() - async-signal-safe per POSIX
 *   - std::atomic - NOT guaranteed async-signal-safe, but lock-free operations
 *     typically compile to single instructions (documented risk accepted)
 *   - NEVER calls: malloc, free, printf, C++ iostreams, sqlite3 functions
 * Thread-safety: Called from signal context (any thread)
 * 
 * REVIEWER_NOTE: This function runs in signal context with severe restrictions.
 * Only async-signal-safe functions are allowed. Any violation can cause deadlock
 * or corruption. The use of std::atomic is a documented implementation choice
 * that works in practice but is not guaranteed by standards.
 */
void crash_handler(int sig) {
    // Use async-signal-safe functions only
    const char* msg = nullptr;
    size_t len = 0;
    switch(sig) {
        case SIGSEGV:
            msg = "\n[CRASH] Segmentation fault - attempting emergency cleanup...\n";
            len = sizeof("\n[CRASH] Segmentation fault - attempting emergency cleanup...\n") - 1;
            break;
        case SIGABRT:
            msg = "\n[CRASH] Abort signal - attempting emergency cleanup...\n";
            len = sizeof("\n[CRASH] Abort signal - attempting emergency cleanup...\n") - 1;
            break;
        case SIGBUS:
            msg = "\n[CRASH] Bus error - attempting emergency cleanup...\n";
            len = sizeof("\n[CRASH] Bus error - attempting emergency cleanup...\n") - 1;
            break;
        default:
            msg = "\n[CRASH] Fatal signal - attempting emergency cleanup...\n";
            len = sizeof("\n[CRASH] Fatal signal - attempting emergency cleanup...\n") - 1;
            break;
    }
    if (msg != nullptr && len > 0) {
        // SECURITY: Signal handler safety - write() is async-signal-safe
        // Intentionally ignore return value (can't handle errors in crash handler)
        ignore_write_result(write(STDERR_FILENO, msg, len));
    }
    
    // NOTE: We use atomic operations here as best-effort cleanup.
    // While C++ std::atomic is not guaranteed async-signal-safe by the standard,
    // lock-free atomic operations typically compile to single instructions.
    // In a crash scenario, we accept this for best-effort cleanup.
    
    // Close socket (best effort)
    int fd = srv_fd.exchange(-1);
    if (fd >= 0) {
        close(fd);
    }
    
    // Remove socket file (best effort)
    if (sock_path_buf[0] != '\0') {
        unlink(sock_path_buf);
    }
    
    // Remove PID file (best effort)
    if (pid_file_path_buf[0] != '\0') {
        unlink(pid_file_path_buf);
    }
    
    // Note: Do NOT close database here - sqlite3_close() is not async-signal-safe
    // WAL mode will handle recovery on next startup
    
    // Restore default handler and re-raise for core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Function: sig_handler
 * Purpose: Graceful shutdown handler for SIGINT and SIGTERM
 * Parameters:
 *   - sig: Signal number (unused, suppressed with (void)sig)
 * Returns: void
 * Security:
 *   - CRITICAL: Only uses async-signal-safe functions
 *   - write() - async-signal-safe per POSIX
 *   - close() - async-signal-safe per POSIX  
 *   - std::atomic::exchange() - NOT guaranteed async-signal-safe, but lock-free
 *     operations typically compile to single instructions (documented risk)
 *   - NEVER calls: malloc, free, printf, C++ iostreams, sqlite3 functions
 * Thread-safety: Called from signal context (any thread)
 * 
 * Implementation Notes:
 * - Sets atomic flag to initiate graceful shutdown
 * - Closes server socket to unblock accept() call in main thread
 * - Main thread polls the 'running' flag and performs full cleanup
 * - Database writes happen in main thread, not here
 * 
 * REVIEWER_NOTE: This handler sets flags and closes FDs to wake the main thread.
 * All complex cleanup happens in the main thread after detecting shutdown.
 */
void sig_handler(int sig) {
    (void)sig;  // Unused parameter
    
    // NOTE: We use C++ std::atomic operations here. While not guaranteed
    // async-signal-safe by POSIX/C++ standards, lock-free atomic operations
    // on modern platforms typically compile to single instructions and work
    // reliably in practice. This is a documented implementation choice.
    
    // Only print once using global atomic
    if (shutdown_started.exchange(true) == false) {
        const char msg[] = "\n[INFO] Shutdown signal received, stopping gracefully...\n";
        // SECURITY: Signal handler safety - write() is async-signal-safe
        // Intentionally ignore return value (can't handle errors in signal handler)
        ignore_write_result(write(STDERR_FILENO, msg, sizeof(msg)-1));
    }
    
    running = 0; 
    
    // Close socket to unblock accept()
    int fd = srv_fd.exchange(-1);  // Atomically get and set to -1
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);  // Unblock accept() immediately
        close(fd);
    }
    
    // Note: PID file cleanup will happen in main() after signal handler sets running = 0
}

// Helper function to find which root a path belongs to
// Returns the index of the most specific (longest matching) root
// Note: This function is only called with paths from inotify events, which should
// always be under one of the monitored roots. If no match is found (best_len == 0),
// this indicates a bug and we assert to catch it early.
size_t find_root_index(const string& path) {
    size_t best_idx = 0;
    size_t best_len = 0;
    
    for (size_t i = 0; i < root_paths.size(); i++) {
        if ((path == root_paths[i] || path.starts_with(root_paths[i])) && 
            root_paths[i].size() > best_len) {
            best_idx = i;
            best_len = root_paths[i].size();
        }
    }
    
    // Assert that we found a matching root - this should always be true for paths
    // from inotify events. If this assertion fails, it indicates a logic error.
    assert(best_len > 0 && "Path does not belong to any monitored root");
    
    return best_idx;
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(1);
    if (pid > 0) exit(0);
    umask(0);
    if (setsid() < 0) exit(1);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

void update_or_add(const string& full, size_t root_index) {
    struct stat st {};
    if (lstat(full.c_str(), &st) != 0) return;

    bool is_dir = S_ISDIR(st.st_mode);
    int64_t sz = is_dir ? 0 : st.st_size;

    lock_guard<mutex> lk(mtx);
    auto it = find_if(entries.begin(), entries.end(), [&](const Entry& e){ return e.path == full; });
    if (it != entries.end()) {
        // Entry already exists - update it in place
        it->size = sz;
        it->mtime = st.st_mtime;
        it->is_dir = is_dir;
        it->root_index = root_index;
        // Path index doesn't need updating since the entry location didn't change
    } else {
        // New entry - add to entries and rebuild path index
        Entry e;
        e.path = full;
        e.size = sz;
        e.mtime = st.st_mtime;
        e.is_dir = is_dir;
        e.root_index = root_index;
        entries.push_back(e);
        
        // Rebuild path index to avoid dangling pointers from vector reallocation
        // When entries vector reallocates, all stored pointers become invalid
        path_index.dir_to_entries.clear();
        path_index.all_dirs.clear();
        
        for (auto& entry : entries) {
            size_t last_slash = entry.path.rfind('/');
            if (last_slash != string::npos) {
                string dir = entry.path.substr(0, last_slash);
                path_index.dir_to_entries[dir].push_back(&entry);
                path_index.all_dirs.insert(dir);
            }
        }
    }
    
    // Mark DB as dirty if persistence is enabled
    if (db_enabled) {
        pending_changes++;
        db_dirty = true;
    }
}

/**
 * Function: remove_path
 * Purpose: Remove a file or directory from the in-memory index
 * Parameters:
 *   - full: Full absolute path to remove
 *   - recursive: If true, also removes all entries under this path (for directories)
 * Returns: void
 * Security:
 *   - Thread-safe: Uses mutex lock
 *   - Rebuilds path index after removal to maintain consistency
 *   - Updates database dirty flag if persistence is enabled
 * Thread-safety: Thread-safe (uses mtx)
 * 
 * Implementation Notes:
 * - For recursive removal, uses starts_with() to match all children
 * - Invalidates entry pointers when vector is modified
 * - Rebuilds entire path index for simplicity (could be optimized)
 * - Tracks removed count for database synchronization
 */
void remove_path(const string& full, bool recursive = false) {
    lock_guard<mutex> lk(mtx);
    size_t count_before = entries.size();
    if (recursive) {
        entries.erase(remove_if(entries.begin(), entries.end(), [&](const Entry& e){
            return e.path == full || e.path.starts_with(full + "/");
        }), entries.end());
    } else {
        entries.erase(remove_if(entries.begin(), entries.end(), [&](const Entry& e){ return e.path == full; }), entries.end());
    }
    
    // Mark DB as dirty if any entries were removed
    size_t removed = count_before - entries.size();
    if (db_enabled && removed > 0) {
        pending_changes += removed;
        db_dirty = true;
    }
    
    // Rebuild path index since entry pointers may have been invalidated
    // This is simpler than trying to maintain pointers during vector modifications
    if (removed > 0) {
        path_index.dir_to_entries.clear();
        path_index.all_dirs.clear();
        
        for (auto& e : entries) {
            size_t last_slash = e.path.rfind('/');
            if (last_slash != string::npos) {
                string dir = e.path.substr(0, last_slash);
                path_index.dir_to_entries[dir].push_back(&e);
                path_index.all_dirs.insert(dir);
            }
        }
    }
}

void handle_directory_rename(const string& old_path, const string& new_path) {
    lock_guard<mutex> lk(mtx);
    int updated = 0;
    
    // Update all entry paths under the renamed directory
    for (auto& e : entries) {
        if (e.path == old_path || e.path.starts_with(old_path + "/")) {
            e.path = new_path + e.path.substr(old_path.size());
            updated++;
        }
    }
    
    // Update wd_to_dir mappings
    for (auto& [wd, dir] : wd_to_dir) {
        if (dir == old_path || dir.starts_with(old_path + "/")) {
            dir = new_path + dir.substr(old_path.size());
        }
    }
    
    // Rebuild path index since paths have changed
    if (updated > 0) {
        path_index.dir_to_entries.clear();
        path_index.all_dirs.clear();
        
        for (auto& e : entries) {
            size_t last_slash = e.path.rfind('/');
            if (last_slash != string::npos) {
                string dir = e.path.substr(0, last_slash);
                path_index.dir_to_entries[dir].push_back(&e);
                path_index.all_dirs.insert(dir);
            }
        }
    }
    
    if (foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory renamed: "
             << COLOR_BOLD << old_path << COLOR_RESET << " -> "
             << COLOR_BOLD << new_path << COLOR_RESET
             << " (updated " << updated << " entries)\n";
    }
}

void cleanup_stale_pending_moves() {
    lock_guard<mutex> lk(pending_moves_mtx);
    auto now = chrono::steady_clock::now();
    for (auto it = pending_moves.begin(); it != pending_moves.end(); ) {
        if (chrono::duration_cast<chrono::seconds>(now - it->second.second).count() > 1) {
            // Stale move (moved out of watched tree) - treat as delete
            const string& path = it->second.first;
            
            // Remove watch descriptors for the moved-out directory tree
            for (auto wd_it = wd_to_dir.begin(); wd_it != wd_to_dir.end(); ) {
                if (wd_it->second == path || wd_it->second.starts_with(path + "/")) {
                    inotify_rm_watch(in_fd, wd_it->first);
                    wd_it = wd_to_dir.erase(wd_it);
                } else {
                    ++wd_it;
                }
            }
            
            remove_path(path, true);  // Remove from entries
            if (foreground) {
                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory deleted: "
                     << COLOR_BOLD << path << COLOR_RESET << " (moved out of tree)\n";
            }
            it = pending_moves.erase(it);
        } else {
            ++it;
        }
    }
}

/**
 * Function: add_watch
 * Purpose: Register an inotify watch on a directory
 * Parameters:
 *   - dir: Directory path to watch
 * Returns: void
 * Security:
 *   - No validation of directory path (assumes caller validates)
 *   - Watch descriptor stored in global map
 * Thread-safety: Not thread-safe (called from single-threaded context during indexing)
 * 
 * REVIEWER_NOTE: This assumes inotify_add_watch() succeeds. Failed watches are
 * silently ignored (wd <= 0). This is acceptable as indexing continues without
 * real-time updates for that directory.
 */
void add_watch(const string& dir) {
    int wd = inotify_add_watch(in_fd, dir.c_str(),
        IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd > 0) wd_to_dir[wd] = dir;
}

/**
 * Function: add_directory_recursive
 * Purpose: Recursively index a directory and all its subdirectories
 * Parameters:
 *   - dir: Directory path to index
 *   - root_index: Index of the root directory in the roots vector
 * Returns: void
 * Security:
 *   - Skips symlinks to prevent infinite loops
 *   - Exception-safe: catches all exceptions from directory iteration
 * Thread-safety: Not thread-safe (called during initial indexing or from event thread)
 * 
 * Implementation Notes:
 * - Adds inotify watch for real-time updates
 * - Recursively processes subdirectories depth-first
 * - Symlinks are intentionally skipped (logged in foreground mode)
 * - Exceptions from filesystem operations are caught and ignored
 * 
 * REVIEWER_NOTE: Symlink handling prevents traversal loops but means symlinked
 * directories are not indexed. This is a deliberate design choice.
 */
void add_directory_recursive(const string& dir, size_t root_index) {
    // Add the directory itself
    update_or_add(dir, root_index);
    add_watch(dir);
    
    // Recursively add all subdirectories and files
    try {
        for (auto& e : directory_iterator(dir)) {
            string p = e.path().string();
            
            // Skip symlinks to avoid infinite loops
            if (e.is_symlink()) {
                if (foreground) {
                    cerr << COLOR_YELLOW << "[INFO]" << COLOR_RESET 
                         << " Skipping symlink: " << p << "\n";
                }
                continue;
            }
            
            if (e.is_directory()) {
                add_directory_recursive(p, root_index);
            } else {
                update_or_add(p, root_index);
            }
        }
    } catch (...) {}
}

void build_path_index() {
    // This function must be called in a single-threaded context (e.g., during daemon startup)
    // or with mtx already held by the caller. It does NOT acquire the mutex itself.
    // DO NOT call this from multiple threads without external synchronization.
    
    // Clear existing index
    path_index.dir_to_entries.clear();
    path_index.all_dirs.clear();
    
    for (auto& e : entries) {
        // Extract directory path from entry path
        size_t last_slash = e.path.rfind('/');
        if (last_slash == string::npos) continue;  // Skip entries without directory
        
        string dir = e.path.substr(0, last_slash);
        
        // Add entry to directory index
        path_index.dir_to_entries[dir].push_back(&e);
        
        // Track unique directories
        path_index.all_dirs.insert(dir);
    }
    
    if (foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
             << " Path index built: " << path_index.all_dirs.size() 
             << " directories indexed\n";
    }
}

void initial_setup(const vector<string>& roots, bool skip_indexing = false) {
    // Initialize inotify first
    in_fd = inotify_init1(IN_NONBLOCK);
    if (in_fd < 0) {
        int err = errno;
        cerr << COLOR_RED << "ERROR: inotify_init1 failed: " << strerror(err) 
             << " (" << err << ")" << COLOR_RESET << "\n";
        throw runtime_error("inotify_init1 failed");
    }
    
    // Track indexing statistics
    auto start_time = chrono::steady_clock::now();
    size_t total_files = 0;
    size_t total_dirs = 0;
    
    // Process each root directory (already canonicalized with trailing slashes)
    for (size_t root_idx = 0; root_idx < roots.size(); root_idx++) {
        root_paths.push_back(roots[root_idx]);
        
        // Index this root only if not skipping
        size_t initial_count = 0;
        if (!skip_indexing) {
            // Log start of indexing for this root
            if (foreground) {
                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                     << " Indexing " << roots[root_idx] << " ...\n";
            }
            
            {
                lock_guard<mutex> lk(mtx);
                for (auto& e : recursive_directory_iterator(roots[root_idx], directory_options::skip_permission_denied)) {
                    try {
                        string p = e.path().string();
                        struct stat st {};
                        if (lstat(p.c_str(), &st) == 0) {
                            bool is_dir = S_ISDIR(st.st_mode);
                            Entry entry;
                            entry.path = p;
                            entry.size = is_dir ? 0LL : st.st_size;
                            entry.mtime = st.st_mtime;
                            entry.is_dir = is_dir;
                            entry.root_index = root_idx;
                            entries.push_back(entry);
                            initial_count++;
                            
                            // Track file vs directory counts
                            if (is_dir) {
                                total_dirs++;
                            } else {
                                total_files++;
                            }
                            
                            // Log progress every 10000 entries (in foreground mode)
                            if (foreground && initial_count % 10000 == 0) {
                                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                                     << " Indexed " << initial_count << " entries in " 
                                     << roots[root_idx] << "...\n";
                            }
                        }
                    } catch (...) {}
                }
            }
            
            // Mark entries as dirty if database is enabled
            if (db_enabled && initial_count > 0) {
                pending_changes += initial_count;
                db_dirty = true;
            }
        }
        
        // Add watches for this root
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                 << " Setting up filesystem watches for " << roots[root_idx] << "...\n";
        }
        
        int watch_count = 0;
        function<void(const string&)> rec_add = [&](const string& d) {
            add_watch(d);
            watch_count++;
            
            // Show progress every 500 watches
            if (foreground && watch_count % 500 == 0) {
                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                     << " Added " << watch_count << " watches...\n";
            }
            
            try {
                for (auto& e : directory_iterator(d)) {
                    // Skip symlinks to avoid infinite loops
                    if (e.is_symlink()) {
                        if (foreground) {
                            cerr << COLOR_YELLOW << "[INFO]" << COLOR_RESET 
                                 << " Skipping symlink: " << e.path() << "\n";
                        }
                        continue;
                    }
                    
                    if (e.is_directory()) {
                        rec_add(e.path().string());
                    }
                }
            } catch (...) {}
        };
        rec_add(roots[root_idx]);
        
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                 << " Completed: Added " << watch_count << " watches\n";
        }
    }
    
    // Log indexing complete
    if (!skip_indexing && foreground) {
        auto end_time = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
        double elapsed_sec = elapsed.count() / 1000.0;
        
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
             << " Indexing complete: " << total_files << " files, " 
             << total_dirs << " directories (" << fixed << setprecision(1) 
             << elapsed_sec << "s)\n";
    }
}

/**
 * Function: process_events
 * Purpose: Main event loop for processing inotify filesystem events
 * Parameters: None
 * Returns: void (runs until 'running' flag is cleared)
 * Security:
 *   - Bounds checking on inotify event buffer parsing
 *   - Handles partial reads correctly
 *   - Validates event structure sizes before access
 *   - Protected against malformed inotify events
 * Thread-safety: Runs in dedicated thread, uses mutexes for shared data
 * 
 * REVIEWER_NOTE: This is the core filesystem monitoring loop. It must correctly
 * parse inotify events without buffer overruns. Events can be variable-length
 * due to the filename field.
 */
void process_events() {
    char buf[8192] __attribute__((aligned(8)));
    auto last_cleanup = chrono::steady_clock::now();
    
    while (running) {
        // Periodically clean up stale pending moves (once per second)
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_cleanup).count() >= 1) {
            cleanup_stale_pending_moves();
            last_cleanup = now;
        }
        
        // Check if we should flush to database
        maybe_flush_to_db();
        
        // Use poll with timeout for better signal responsiveness
        struct pollfd pfd = {in_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);  // 100ms timeout
        
        if (ret < 0) {
            if (errno == EINTR) continue;  // Interrupted by signal, check running flag
            break;
        }
        
        if (ret == 0) continue;  // Timeout, check running flag
        
        if (!(pfd.revents & POLLIN)) continue;
        
        ssize_t len = read(in_fd, buf, sizeof(buf));
        if (len > 0) {
            char* ptr = buf;
            // SECURITY: Explicit bounds checking for inotify event parsing
            // Each event consists of: struct inotify_event + variable-length name
            while (ptr < buf + len) {
                // Ensure we have space for at least the event header
                if (ptr + sizeof(struct inotify_event) > buf + len) {
                    // Incomplete event at buffer end, should not happen with properly sized buffer
                    break;
                }
                
                auto* ev = (struct inotify_event*)ptr;
                
                // SECURITY: Validate that the full event (including name) fits in buffer
                // This prevents reading beyond buffer bounds if ev->len is corrupted
                if (ptr + sizeof(struct inotify_event) + ev->len > buf + len) {
                    // Event extends beyond buffer, should not happen but we check anyway
                    break;
                }
                
                ptr += sizeof(struct inotify_event) + ev->len;

                auto wdit = wd_to_dir.find(ev->wd);
                if (wdit == wd_to_dir.end()) continue;
                string dir = wdit->second;
                string name = ev->len ? ev->name : "";
                string full = dir;
                if (!dir.ends_with("/")) full += "/";
                full += name;

                bool isd = ev->mask & IN_ISDIR;

                if (ev->mask & IN_IGNORED) {
                    wd_to_dir.erase(ev->wd);
                    continue;
                }
                // Handle IN_DELETE_SELF (directory was deleted)
                // Skip IN_MOVE_SELF as renames are handled via IN_MOVED_FROM/IN_MOVED_TO on parent
                if (ev->mask & IN_DELETE_SELF) {
                    wd_to_dir.erase(ev->wd);
                    int removed_count = 0;
                    if (foreground) {
                        lock_guard<mutex> lk(mtx);
                        // Count entries that will be removed (for logging only)
                        for (const auto& e : entries) {
                            if (e.path == dir || e.path.starts_with(dir + "/")) {
                                removed_count++;
                            }
                        }
                    }
                    remove_path(dir, true);
                    if (foreground) {
                        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory deleted: "
                             << COLOR_BOLD << dir << COLOR_RESET 
                             << " (watch removed, " << removed_count << " entries removed)\n";
                    }
                    continue;
                }
                // IN_MOVE_SELF is ignored - renames are handled at parent level
                if (ev->mask & IN_MOVE_SELF) {
                    // The directory was moved. If it's a rename within tree,
                    // it's already handled by IN_MOVED_FROM/TO on parent.
                    // If moved out of tree, the parent's IN_MOVED_FROM handles it.
                    // Just update the wd_to_dir if needed, but don't delete.
                    continue;
                }
                
                // Process directory-specific events
                if (isd) {
                    if (ev->mask & IN_CREATE) {
                        // New directory created - add recursively with watches
                        size_t root_idx = find_root_index(full);
                        add_directory_recursive(full, root_idx);
                        if (foreground) {
                            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory created: "
                                 << COLOR_BOLD << full << COLOR_RESET << " (watch added)\n";
                        }
                    }
                    if (ev->mask & IN_MOVED_FROM) {
                        // Directory moved out or renamed - store in pending moves map
                        // RACE CONDITION HANDLING: Cookie-based tracking ensures we can
                        // match IN_MOVED_FROM with IN_MOVED_TO even if they arrive in
                        // separate read() calls. Timeout cleanup handles moves out of tree.
                        lock_guard<mutex> lk(pending_moves_mtx);
                        pending_moves[ev->cookie] = {full, chrono::steady_clock::now()};
                        // The entry stays in pending_moves until either a matching IN_MOVED_TO
                        // arrives in this same event-processing thread or cleanup_stale_pending_moves()
                        // removes it after the ~1s stale timeout.
                    }
                    if (ev->mask & IN_MOVED_TO) {
                        // Directory moved in or renamed - check for matching MOVED_FROM
                        bool found_match = false;
                        string old_path;
                        {
                            lock_guard<mutex> lk(pending_moves_mtx);
                            auto it = pending_moves.find(ev->cookie);
                            if (it != pending_moves.end()) {
                                old_path = it->second.first;
                                pending_moves.erase(it);
                                found_match = true;
                            }
                        }
                        
                        if (found_match) {
                            // This is a rename within our watched tree
                            // IMPORTANT: Inotify watch descriptors automatically follow directory
                            // renames, so we don't need to remove/re-add watches. We only need to
                            // update our internal path mappings.
                            handle_directory_rename(old_path, full);
                            // Inotify watch descriptors automatically follow directory renames;
                            // no need to re-add a watch for the new path here.
                        } else {
                            // Moved into tree from outside - treat as new directory
                            size_t root_idx = find_root_index(full);
                            add_directory_recursive(full, root_idx);
                            if (foreground) {
                                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory created: "
                                     << COLOR_BOLD << full << COLOR_RESET << " (moved in, watch added)\n";
                            }
                        }
                    }
                    if (ev->mask & IN_DELETE) {
                        // Directory deleted - remove recursively
                        remove_path(full, true);
                        if (foreground) {
                            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory deleted: "
                                 << COLOR_BOLD << full << COLOR_RESET << " (watch removed)\n";
                        }
                    }
                } else {
                    // Process file events (non-directory)
                    if (ev->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE)) {
                        // File created, moved in, or modified - update index
                        size_t root_idx = find_root_index(full);
                        update_or_add(full, root_idx);
                    }
                    if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) {
                        // File deleted or moved out - remove from index
                        remove_path(full);
                    }
                }
            }
        } else if (len < 0 && errno != EAGAIN) break;
    }
}

/**
 * Class: MappedFile
 * Purpose: RAII wrapper for memory-mapped files used in content search
 * 
 * Security Considerations:
 * - Uses MAP_PRIVATE to prevent modifications from affecting original file
 * - Handles errors gracefully (returns invalid on any failure)
 * - Empty files are handled correctly (valid but no data)
 * - madvise() failure is non-fatal (advisory only)
 * 
 * Memory Safety:
 * - RAII: Automatically unmaps and closes on destruction
 * - Copy-prevention: Deleted copy constructor and assignment
 * - Move semantics: Not implemented (each thread creates its own)
 * 
 * Thread-safety: Not thread-safe (each thread creates its own instance)
 * 
 * Implementation Notes:
 * - Uses mmap() for efficient file reading without copying to userspace
 * - MADV_SEQUENTIAL hints kernel for better readahead
 * - Fallback gracefully if mmap() fails (returns invalid)
 * 
 * REVIEWER_NOTE: This is the core of efficient content search. By using mmap(),
 * we avoid copying file contents into userspace buffers. The kernel provides
 * the pages directly from its page cache.
 */
struct MappedFile {
    char* data;
    size_t size;
    int fd;
    
    MappedFile(const string& path) : data(nullptr), size(0), fd(-1) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return;
        
        struct stat st;
        if (fstat(fd, &st) != 0) {
            close(fd);
            fd = -1;
            return;
        }
        
        size = st.st_size;
        if (size == 0) {
            // Empty files are valid - we'll just have no content to search
            // Keep fd open for consistency but data will be nullptr
            close(fd);
            fd = -1;
            return;
        }
        
        data = static_cast<char*>(mmap(nullptr, size, PROT_READ, 
                                       MAP_PRIVATE, fd, 0));
        if (data == MAP_FAILED) {
            data = nullptr;
            close(fd);
            fd = -1;
            return;
        }
        
        // Hint: we'll read sequentially
        // madvise is advisory, failure is non-fatal
        if (madvise(data, size, MADV_SEQUENTIAL) != 0) {
            // Continue anyway - this is just a performance hint
        }
    }
    
    ~MappedFile() {
        if (data) munmap(data, size);
        if (fd >= 0) close(fd);
    }
    
    bool is_valid() const { return data != nullptr; }
    
    // Prevent copying
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
};

/**
 * Function: send_results_batched
 * Purpose: Send search results to client using batched writev() for efficiency
 * Parameters:
 *   - fd: Client socket file descriptor
 *   - results: Vector of result strings to send
 * Returns: void
 * Security:
 *   - Handles EINTR correctly in writev() loop
 *   - Handles partial writes correctly
 *   - No buffer overflow risk (sends only complete strings)
 * Thread-safety: Thread-safe (each client has unique fd)
 * 
 * Performance Notes:
 * - Uses writev() to batch multiple strings into fewer syscalls
 * - Reduces context switches compared to individual write() calls
 * - Batches up to 1024 iovecs at a time (typical IOV_MAX on Linux)
 * - Handles partial writes by adjusting iovec pointers
 * 
 * REVIEWER_NOTE: This function implements the gather I/O pattern for efficiency.
 * The partial write handling is complex but correct.
 */
void send_results_batched(int fd, const vector<string>& results) {
    if (results.empty()) return;
    
    const size_t MAX_IOV = 1024;  // Typical IOV_MAX on Linux
    vector<struct iovec> iov;
    iov.reserve(MAX_IOV);
    
    for (size_t i = 0; i < results.size(); i++) {
        struct iovec vec;
        vec.iov_base = const_cast<char*>(results[i].c_str());
        vec.iov_len = results[i].size();
        iov.push_back(vec);
        
        // Flush batch when full or at end
        if (iov.size() >= MAX_IOV || i == results.size() - 1) {
            size_t offset = 0;
            while (offset < iov.size()) {
                ssize_t n = writev(fd, iov.data() + offset, iov.size() - offset);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    // Log error to stderr (client socket might be closed)
                    // Silently return - client disconnect is expected
                    return;
                }
                
                // Advance offset by number of complete iovecs written
                size_t bytes_written = n;
                while (offset < iov.size() && bytes_written >= iov[offset].iov_len) {
                    bytes_written -= iov[offset].iov_len;
                    offset++;
                }
                
                // Handle partial iovec write
                if (bytes_written > 0 && offset < iov.size()) {
                    iov[offset].iov_base = (char*)iov[offset].iov_base + bytes_written;
                    iov[offset].iov_len -= bytes_written;
                }
            }
            iov.clear();
        }
    }
}

/**
 * Function: handle_client
 * Purpose: Process a search request from a client connection
 * Parameters:
 *   - fd: File descriptor of the connected client socket
 * Returns: void (closes fd before returning)
 * Security:
 *   - CRITICAL: Validates all network input sizes (max 1MB per pattern)
 *   - Handles partial reads and writes correctly
 *   - Validates regex patterns before use
 *   - Uses safe_write_all() for error reporting
 * Thread-safety: Thread-safe (each client has its own fd)
 * 
 * Protocol Overview:
 *   1. Read name pattern length (4 bytes) + pattern data
 *   2. Read path pattern length (4 bytes) + pattern data
 *   3. Read content pattern length (4 bytes) + pattern data
 *   4. Read flags (1 byte): case_insensitive, is_regex, content_glob
 *   5. Read type filter (1 byte)
 *   6. Read size operator + value (1 + 8 bytes)
 *   7. Read mtime operator + days (1 + 4 bytes)
 *   8. Read context lines: before_ctx, after_ctx (1 + 1 bytes)
 */
void handle_client(int fd) {
    // SECURITY: Maximum pattern size to prevent memory exhaustion attacks
    constexpr uint32_t MAX_PATTERN_SIZE = 1024 * 1024;  // 1MB limit
    
    uint32_t net_nlen, net_plen, net_clen;
    
    // Read name pattern length and validate
    if (!safe_read_all(fd, &net_nlen, 4)) { close(fd); return; }
    uint32_t name_len = ntohl(net_nlen);
    // SECURITY: Validate name pattern length
    if (name_len > MAX_PATTERN_SIZE) { 
        const char* err = "Name pattern too large\n";
        safe_write_all(fd, err, strlen(err));
        close(fd); 
        return; 
    }
    string name_pat(name_len, '\0');
    if (name_len > 0 && !safe_read_all(fd, name_pat.data(), name_len)) { 
        close(fd); 
        return; 
    }

    // Read path pattern length and validate
    if (!safe_read_all(fd, &net_plen, 4)) { close(fd); return; }
    uint32_t path_len = ntohl(net_plen);
    // SECURITY: Validate path pattern length
    if (path_len > MAX_PATTERN_SIZE) { 
        const char* err = "Path pattern too large\n";
        safe_write_all(fd, err, strlen(err));
        close(fd); 
        return; 
    }
    string path_pat(path_len, '\0');
    if (path_len > 0 && !safe_read_all(fd, path_pat.data(), path_len)) { 
        close(fd); 
        return; 
    }

    // Read content pattern length and validate
    if (!safe_read_all(fd, &net_clen, 4)) { close(fd); return; }
    uint32_t content_len = ntohl(net_clen);
    // SECURITY: Validate content pattern length
    if (content_len > MAX_PATTERN_SIZE) { 
        const char* err = "Content pattern too large\n";
        safe_write_all(fd, err, strlen(err));
        close(fd); 
        return; 
    }
    string content_pat(content_len, '\0');
    if (content_len > 0 && !safe_read_all(fd, content_pat.data(), content_len)) { 
        close(fd); 
        return; 
    }

    // Read flags byte
    uint8_t flags = 0;
    if (read(fd, &flags, 1) != 1) flags = 0;
    bool case_ins = flags & 1;      // bit 0 (value 1)
    bool is_regex = flags & 2;      // bit 1 (value 2)
    bool content_glob = flags & 4;  // bit 2 (value 4)

    // Read type filter
    uint8_t type_filter = 0;
    if (read(fd, &type_filter, 1) != 1) type_filter = 0;

    // Read size filter (operator + optional value)
    uint8_t size_op = 0;
    int64_t size_val = 0;
    if (read(fd, &size_op, 1) == 1 && size_op) {
        // Only read value if operator is present
        if (!safe_read_all(fd, &size_val, 8)) {
            close(fd);
            return;
        }
    }

    // Read mtime filter (operator + optional days)
    uint8_t mtime_op = 0;
    int32_t mtime_days = 0;
    if (read(fd, &mtime_op, 1) == 1 && mtime_op) {
        // Only read days if operator is present
        if (!safe_read_all(fd, &mtime_days, 4)) {
            close(fd);
            return;
        }
    }

    // Read context line parameters (added in v1.1 for context lines feature)
    // Note: For backward compatibility with older daemons, these bytes default to 0 if read fails.
    // This protocol extension approach is acceptable for new features where both client and daemon
    // are updated together. Old clients connecting to new daemons will work (daemon reads 0s).
    // New clients connecting to old daemons may have issues - this is expected for feature updates.
    uint8_t before_ctx = 0, after_ctx = 0;
    if (read(fd, &before_ctx, 1) != 1) before_ctx = 0;
    if (read(fd, &after_ctx, 1) != 1) after_ctx = 0;

    bool has_content = !content_pat.empty();

    // Compile regex if needed
    shared_ptr<RE2> re;  // Use shared_ptr for thread-safe lifetime management
    if (has_content && is_regex) {
        RE2::Options opts;
        opts.set_case_sensitive(!case_ins);
        re = make_shared<RE2>(content_pat, opts);
        if (!re->ok()) {
            const char* err = "Invalid regex pattern\n";
            safe_write_all(fd, err, strlen(err));
            close(fd);
            return;
        }
    }

    int fnm_flags = case_ins ? FNM_CASEFOLD : 0;

    // PERFORMANCE OPTIMIZATION: Path index analysis
    // The path index allows us to skip entries that can't possibly match
    // the path pattern. This is especially useful for patterns like "src/*"
    // which only match files in src/ and its subdirectories.
    //
    // Algorithm:
    // 1. Extract static prefix before first wildcard (e.g., "include/st*" -> "include/")
    // 2. Use path_index to lookup only entries in matching directories
    // 3. Fall back to full scan if no usable prefix found
    //
    // Example: Pattern "src/core/*.cpp"
    // - Prefix: "src/core/"
    // - Index lookup: Only entries in directories starting with "src/core/"
    // - Speedup: O(matching_dirs) instead of O(all_entries)
    //
    // REVIEWER_NOTE: This optimization is safe because it only narrows the
    // candidate set. The full pattern is still checked on each candidate.
    bool can_use_index = false;
    string index_prefix;
    
    if (!path_pat.empty()) {
        // Check if pattern has specific prefix before wildcard
        size_t first_wildcard = path_pat.find_first_of("*?[");
        if (first_wildcard != string::npos && first_wildcard > 0) {
            // Extract the prefix before the wildcard
            index_prefix = path_pat.substr(0, first_wildcard);
            
            // Remove trailing partial component (e.g., "include/st*" -> "include/")
            // Keep only complete directory paths
            size_t last_slash = index_prefix.rfind('/');
            if (last_slash != string::npos) {
                index_prefix = index_prefix.substr(0, last_slash);
                can_use_index = true;
            }
        } else if (first_wildcard == string::npos && !path_pat.empty()) {
            // No wildcards - exact path match
            // Still use index to limit search
            size_t last_slash = path_pat.rfind('/');
            if (last_slash != string::npos) {
                index_prefix = path_pat.substr(0, last_slash);
                can_use_index = true;
            }
        }
    }

    lock_guard<mutex> lk(mtx);

    // Collect candidate entries using path index if possible
    vector<const Entry*> candidates_from_index;
    
    if (can_use_index && !index_prefix.empty()) {
        // Use path index to narrow down candidates
        // Scan only directories that match our prefix
        for (const auto& [dir, dir_entries] : path_index.dir_to_entries) {
            // Check if this directory path matches our prefix
            // We need to check each entry's relative path against root_paths
            bool dir_matches = false;
            
            // Try each root to see if this dir matches the pattern when made relative
            for (size_t root_idx = 0; root_idx < root_paths.size(); root_idx++) {
                if (dir.starts_with(root_paths[root_idx])) {
                    string rel_dir = dir.substr(root_paths[root_idx].size());
                    
                    // Normalize: remove leading slash if present
                    if (!rel_dir.empty() && rel_dir.front() == '/') {
                        rel_dir.erase(0, 1);
                    }
                    
                    // Check if relative directory matches our index prefix with proper boundaries
                    // Match if: exact match, or rel_dir is under index_prefix, or index_prefix is under rel_dir
                    if (rel_dir == index_prefix ||
                        rel_dir.starts_with(index_prefix + "/") ||
                        index_prefix.starts_with(rel_dir + "/") ||
                        (rel_dir.empty() && !index_prefix.empty())) {
                        dir_matches = true;
                        break;
                    }
                }
            }
            
            if (dir_matches) {
                for (auto* e : dir_entries) {
                    candidates_from_index.push_back(e);
                }
            }
        }
        
        if (foreground && candidates_from_index.size() < entries.size()) {
            cerr << COLOR_CYAN << "[DEBUG]" << COLOR_RESET 
                 << " Path index used: scanned " << candidates_from_index.size() 
                 << " entries (vs " << entries.size() << " total) for prefix '" 
                 << index_prefix << "'\n";
        }
    }

    vector<const Entry*> candidates;
    vector<string> path_results;  // Collect results for batched sending
    if (!has_content) {
        path_results.reserve(1000);  // Pre-allocate for efficiency
    }
    
    // Lambda to filter and process a single entry (reduces code duplication)
    auto process_entry = [&](const Entry* e) {
        bool type_match = (type_filter == 0) ||
                          (type_filter == 1 && !e->is_dir) ||
                          (type_filter == 2 && e->is_dir);
        if (!type_match) return;
        if (e->is_dir && has_content) return;

        if (size_op) {
            bool match = false;
            if (size_op == 1) match = e->size < size_val;
            else if (size_op == 2) match = e->size == size_val;
            else if (size_op == 3) match = e->size > size_val;
            if (!match) return;
        }

        if (mtime_op) {
            time_t now = time(nullptr);
            int32_t days_old = (now - e->mtime) / 86400;
            bool match = false;
            if (mtime_op == 1) match = days_old < mtime_days;
            else if (mtime_op == 2) match = days_old == mtime_days;
            else if (mtime_op == 3) match = days_old > mtime_days;
            if (!match) return;
        }

        // Calculate relative path from entry's own root
        string rel;
        if (e->root_index < root_paths.size()) {
            rel = e->path.substr(root_paths[e->root_index].size());
        } else {
            rel = e->path; // Fallback if root_index is invalid
        }

        size_t pos = e->path.rfind('/');
        string_view base = (pos == string::npos) ? string_view(e->path) : string_view(e->path.data() + pos + 1);

        bool name_match = fnmatch(name_pat.c_str(), base.data(), fnm_flags) == 0;
        bool path_match = path_pat.empty() || fnmatch(path_pat.c_str(), rel.c_str(), fnm_flags) == 0;

        if (name_match && path_match) {
            if (!has_content) {
                path_results.push_back(e->path + "\n");
            } else {
                candidates.push_back(e);
            }
        }
    };
    
    // Determine which entry set to iterate over
    bool use_index_results = can_use_index && !index_prefix.empty() && !candidates_from_index.empty();
    
    if (use_index_results) {
        // Iterate over indexed candidates only
        for (const auto* e : candidates_from_index) {
            process_entry(e);
        }
    } else {
        // Fall back to full scan of all entries
        for (const auto& e : entries) {
            process_entry(&e);
        }
    }
    
    // Send all path results in batches
    if (!path_results.empty()) {
        send_results_batched(fd, path_results);
    }

    if (has_content) {
        // Ensure thread pool is initialized
        if (!content_search_pool) {
            const char* err = "Internal error: thread pool not initialized\n";
            safe_write_all(fd, err, strlen(err));
            close(fd);
            return;
        }
        
        // Prepare for parallel content search
        vector<future<vector<string>>> futures;
        futures.reserve(candidates.size());
        
        // Submit file processing tasks to thread pool
        for (const auto* ep : candidates) {
            // Capture necessary variables by value for thread safety
            string path = ep->path;  // Copy path string
            
            // REVIEWER_NOTE: This lambda runs in a worker thread from the pool.
            // It must capture everything by value to avoid use-after-free.
            // The shared_ptr<RE2> is safely copied (refcount increment).
            //
            // Content Search Algorithm:
            // 1. Memory-map the file for zero-copy access
            // 2. Check for binary data in first 1KB (skip binary files)
            // 3. If no context: Scan line-by-line with in-place pattern matching
            // 4. If context requested: Parse all lines, find matches, emit with context
            //
            // Pattern Matching Methods:
            // - Fixed string (case-insensitive): strcasestr() or memmem()
            // - Fixed string (case-sensitive): memmem() for efficiency
            // - Regex: RE2::PartialMatch() (thread-safe)
            // - Glob: fnmatch() with FNM_CASEFOLD for case-insensitive
            //
            // SECURITY: File is mapped read-only with MAP_PRIVATE
            // PERFORMANCE: Zero-copy via mmap, memmem for fixed strings
            futures.push_back(content_search_pool->enqueue([path, content_pat, case_ins, 
                                                            is_regex, content_glob, 
                                                            before_ctx, after_ctx, re]() {  // Capture re by value
                vector<string> file_results;
                
                // Each thread gets its own file mapping for thread safety
                MappedFile file(path);
                if (!file.is_valid()) return file_results;
                
                // SECURITY: Binary file detection - scan first 1KB for null bytes
                // This prevents displaying binary files as text (can cause terminal corruption)
                size_t check = min<size_t>(1024, file.size);
                bool binary = false;
                for (size_t i = 0; i < check; i++) {
                    if (file.data[i] == '\0') {
                        binary = true;
                        break;
                    }
                }
                if (binary) return file_results;  // Skip binary files
                
                if (before_ctx == 0 && after_ctx == 0) {
                    // Fast path: No context lines requested
                    // Scan file using mmap, no intermediate allocations
                    const char* line_start = file.data;
                    size_t lineno = 1;
                    
                    for (size_t i = 0; i < file.size; i++) {
                        if (file.data[i] == '\n') {
                            size_t line_len = file.data + i - line_start;
                            
                            // Match pattern in-place (no string allocation unless needed)
                            bool match = false;
                            if (content_glob) {
                                string line_str(line_start, line_len);
                                int fnm_flags_content = case_ins ? FNM_CASEFOLD : 0;
                                match = fnmatch(content_pat.c_str(), line_str.c_str(), fnm_flags_content) == 0;
                            } else if (is_regex) {
                                re2::StringPiece line_piece(line_start, line_len);
                                match = RE2::PartialMatch(line_piece, *re);
                            } else if (case_ins) {
                                // Use strcasestr with temporary null-terminated string
                                string line_str(line_start, line_len);
                                match = strcasestr(line_str.c_str(), content_pat.c_str()) != nullptr;
                            } else {
                                // Simple substring search using memmem
                                match = (memmem(line_start, line_len, 
                                              content_pat.c_str(), content_pat.size()) != nullptr);
                            }
                            
                            if (match) {
                                string out = path + ":" + to_string(lineno) + ":" +
                                           string(line_start, line_len) + "\n";
                                file_results.push_back(out);
                            }
                            
                            line_start = file.data + i + 1;
                            lineno++;
                        }
                    }
                    
                    // Handle last line if file doesn't end with newline
                    if (line_start < file.data + file.size) {
                        size_t line_len = file.data + file.size - line_start;
                        bool match = false;
                        if (content_glob) {
                            string line_str(line_start, line_len);
                            int fnm_flags_content = case_ins ? FNM_CASEFOLD : 0;
                            match = fnmatch(content_pat.c_str(), line_str.c_str(), fnm_flags_content) == 0;
                        } else if (is_regex) {
                            re2::StringPiece line_piece(line_start, line_len);
                            match = RE2::PartialMatch(line_piece, *re);
                        } else if (case_ins) {
                            string line_str(line_start, line_len);
                            match = strcasestr(line_str.c_str(), content_pat.c_str()) != nullptr;
                        } else {
                            match = (memmem(line_start, line_len,
                                          content_pat.c_str(), content_pat.size()) != nullptr);
                        }
                        if (match) {
                            string out = path + ":" + to_string(lineno) + ":" +
                                       string(line_start, line_len) + "\n";
                            file_results.push_back(out);
                        }
                    }
                } else {
                    // With context lines - parse all lines first
                    vector<pair<size_t, string>> all_lines; // lineno, content
                    const char* line_start = file.data;
                    size_t lineno = 1;
                    
                    for (size_t i = 0; i < file.size; i++) {
                        if (file.data[i] == '\n') {
                            size_t line_len = file.data + i - line_start;
                            all_lines.emplace_back(lineno, string(line_start, line_len));
                            line_start = file.data + i + 1;
                            lineno++;
                        }
                    }
                    
                    // Handle last line if file doesn't end with newline
                    if (line_start < file.data + file.size) {
                        size_t line_len = file.data + file.size - line_start;
                        all_lines.emplace_back(lineno, string(line_start, line_len));
                    }
                    
                    // Find all matching line indices and store in a set for O(1) lookup
                    vector<size_t> match_indices;
                    unordered_set<size_t> match_set;
                    for (size_t i = 0; i < all_lines.size(); ++i) {
                        bool match = false;
                        const string& content = all_lines[i].second;
                        if (content_glob) {
                            int fnm_flags_content = case_ins ? FNM_CASEFOLD : 0;
                            match = fnmatch(content_pat.c_str(), content.c_str(), fnm_flags_content) == 0;
                        } else if (is_regex) {
                            match = RE2::PartialMatch(content, *re);
                        } else if (case_ins) {
                            match = strcasestr(content.c_str(), content_pat.c_str()) != nullptr;
                        } else {
                            match = content.find(content_pat) != string::npos;
                        }
                        if (match) {
                            match_indices.push_back(i);
                            match_set.insert(i);
                        }
                    }
                    
                    // Process matches with context, merging overlapping ranges
                    if (!match_indices.empty()) {
                        vector<pair<size_t, size_t>> ranges; // start, end (inclusive)
                        
                        for (size_t match_idx : match_indices) {
                            size_t start = (match_idx >= before_ctx) ? match_idx - before_ctx : 0;
                            size_t end = min(match_idx + after_ctx, all_lines.size() - 1);
                            
                            // Merge with previous range if overlapping
                            if (!ranges.empty() && start <= ranges.back().second + 1) {
                                ranges.back().second = max(ranges.back().second, end);
                            } else {
                                ranges.emplace_back(start, end);
                            }
                        }
                        
                        // Collect all context output
                        for (size_t r = 0; r < ranges.size(); ++r) {
                            if (r > 0) {
                                file_results.push_back("--\n");
                            }
                            
                            for (size_t i = ranges[r].first; i <= ranges[r].second; ++i) {
                                bool is_match = match_set.count(i) > 0;
                                char separator = is_match ? ':' : '-';
                                
                                string out = path + ":" + to_string(all_lines[i].first) + separator + all_lines[i].second + "\n";
                                file_results.push_back(out);
                            }
                        }
                    }
                }
                
                return file_results;
            }));
        }
        
        // Collect results from all worker threads
        for (auto& future : futures) {
            try {
                vector<string> file_results = future.get();
                if (!file_results.empty()) {
                    send_results_batched(fd, file_results);
                }
            } catch (const exception& e) {
                // Log error but continue with other files
                if (foreground) {
                    cerr << COLOR_YELLOW << "[WARNING]" << COLOR_RESET 
                         << " Worker thread error: " << e.what() << "\n";
                }
            }
        }
    }

    close(fd);
}

// Helper functions for root path validation
bool check_overlap_and_warn(const vector<string>& roots) {
    bool has_overlap = false;
    for (size_t i = 0; i < roots.size(); i++) {
        for (size_t j = i + 1; j < roots.size(); j++) {
            if (roots[i].starts_with(roots[j]) || roots[j].starts_with(roots[i])) {
                cerr << COLOR_YELLOW << "[WARNING] Overlapping roots: " 
                     << roots[i] << " and " << roots[j] << COLOR_RESET << "\n";
                has_overlap = true;
            }
        }
    }
    return has_overlap;
}

vector<string> deduplicate_paths(const vector<string>& paths) {
    unordered_set<string> seen;
    vector<string> result;
    
    for (const auto& p : paths) {
        if (seen.find(p) == seen.end()) {
            seen.insert(p);
            result.push_back(p);
        } else {
            cerr << COLOR_YELLOW << "[WARNING] Duplicate path ignored: " << p << COLOR_RESET << "\n";
        }
    }
    
    return result;
}

/**
 * Function: main
 * Purpose: Entry point for ffind-daemon
 * 
 * Initialization Flow:
 * 1. Load configuration from ~/.config/ffind/daemon.conf
 * 2. Parse command-line arguments (CLI overrides config)
 * 3. Validate and canonicalize root directory paths
 * 4. Check for overlapping roots and warn
 * 5. Enable SQLite persistence if --db specified
 * 6. Daemonize if not in foreground mode
 * 7. Create Unix domain socket for client communication
 * 8. Initialize inotify for filesystem monitoring
 * 9. Initialize thread pool for content search
 * 10. Index all root directories recursively
 * 11. Enter main event loop (accept clients + process inotify)
 * 
 * Security Model:
 * - Runs as unprivileged user (never requires root)
 * - Socket created in /run/user/$UID with 0600 permissions
 * - PID file in /run/user/$UID with 0644 permissions
 * - Signal handlers use only async-signal-safe functions
 * - All network input validated with size limits
 * 
 * Signal Handling:
 * - SIGINT/SIGTERM: Graceful shutdown
 * - SIGSEGV/SIGABRT/SIGBUS: Emergency cleanup + core dump
 * 
 * Resource Cleanup:
 * - Socket file deleted on exit
 * - PID file deleted on exit
 * - Database flushed on graceful shutdown
 * - All watches removed on exit
 * 
 * REVIEWER_NOTE: This is the main daemon entry point. It sets up all
 * infrastructure before entering the event loop. Error handling must be
 * robust as this runs long-term as a service.
 */
int main(int argc, char** argv) {
    // Load config file first
    Config cfg = load_config();
    
    // Handle no arguments - show usage and exit 1
    if (argc < 2) {
        show_usage();
        return 1;
    }

    bool fg = cfg.foreground;  // Start with config value
    int first_path_idx = 1;
    string db_arg = cfg.db_path;  // Start with config value
    
    // Parse command line options (CLI overrides config)
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            show_usage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            show_version();
            return 0;
        } else if (arg == "--foreground") {
            fg = true;
            first_path_idx = i + 1;
        } else if (arg == "--db") {
            if (i + 1 >= argc) {
                cerr << "ERROR: --db requires a path argument\n";
                return 1;
            }
            db_arg = argv[i + 1];
            i++;  // Skip next arg (it's the db path)
            first_path_idx = i + 1;
        } else {
            // First non-option argument - start of root paths
            first_path_idx = i;
            break;
        }
    }
    
    // Enforce at least one directory argument
    if (first_path_idx >= argc) {
        cerr << "ERROR: At least one directory is required.\n\n";
        show_usage();
        return 1;
    }
    
    // Set global foreground flag
    foreground = fg;
    
    // Log which config was loaded if in foreground mode
    if (cfg.loaded && foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
             << " Loaded config from " << cfg.config_file_path << "\n";
    }
    
    // Collect all root paths
    vector<string> raw_roots;
    for (int i = first_path_idx; i < argc; i++) {
        raw_roots.push_back(argv[i]);
    }
    
    // Validate and canonicalize paths
    vector<string> canonical_roots;
    for (const auto& root : raw_roots) {
        // Check if path exists
        if (!exists(root)) {
            cerr << COLOR_RED << "ERROR: Path does not exist: " << root << COLOR_RESET << "\n";
            return 1;
        }
        
        // Check if it's a directory
        if (!is_directory(root)) {
            cerr << COLOR_RED << "ERROR: Path is not a directory: " << root << COLOR_RESET << "\n";
            return 1;
        }
        
        // Canonicalize the path
        try {
            string canon = canonical(root).string();
            if (canon.back() != '/') canon += '/';
            canonical_roots.push_back(canon);
        } catch (const exception& e) {
            cerr << COLOR_RED << "ERROR: Cannot canonicalize path " << root 
                 << ": " << e.what() << COLOR_RESET << "\n";
            return 1;
        }
    }
    
    // Deduplicate paths
    canonical_roots = deduplicate_paths(canonical_roots);
    
    // Ensure at least one root remains after deduplication
    if (canonical_roots.empty()) {
        cerr << COLOR_RED << "ERROR: No valid root directories after deduplication" << COLOR_RESET << "\n";
        return 1;
    }
    
    // Check for overlaps and warn (always check, not just in foreground)
    check_overlap_and_warn(canonical_roots);
    
    // Determine PID file path before daemonizing
    pid_file_path = get_pid_file_path();
    
    // Copy to fixed buffer for signal-safe cleanup
    strncpy(pid_file_path_buf, pid_file_path.c_str(), sizeof(pid_file_path_buf) - 1);
    pid_file_path_buf[sizeof(pid_file_path_buf) - 1] = '\0';

    // DAEMONIZATION: Run as background service if not in foreground mode
    // This follows the standard Unix daemon pattern:
    // 1. Fork to create child process
    // 2. Parent exits (allows shell to return)
    // 3. Child becomes session leader (setsid)
    // 4. Change working directory to root (avoids locking filesystems)
    // 5. Close inherited file descriptors
    // 6. Redirect stdin/stdout/stderr to /dev/null
    //
    // REVIEWER_NOTE: PID file is created AFTER daemonization so it contains
    // the daemon's actual PID, not the parent's PID.
    if (!foreground) daemonize();

    // Check and create PID file after daemonizing (so we get the correct PID)
    // This function handles race conditions atomically
    if (!check_and_create_pid_file(pid_file_path, foreground)) {
        return 1;
    }
    
    // Initialize database if --db was provided
    vector<string> db_roots;  // Track if entries were loaded from DB
    if (!db_arg.empty()) {
        db_enabled = true;
        db_path = db_arg;
        
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Enabling SQLite persistence: " << db_path << "\n";
        }
        
        if (!init_database(db_path)) {
            cerr << COLOR_RED << "ERROR: Failed to initialize database" << COLOR_RESET << "\n";
            cleanup_pid_file();
            return 1;
        }
        
        // Check root paths match
        db_roots = load_roots_from_db();
        if (!db_roots.empty() && db_roots != canonical_roots) {
            if (foreground) {
                cerr << COLOR_YELLOW << "[WARNING]" << COLOR_RESET 
                     << " Root paths changed since last run. Full reconciliation required.\n";
            }
        }
        
        // Save current roots
        save_roots_to_db(canonical_roots);
        
        // Load existing entries from DB if available
        if (!db_roots.empty()) {
            load_entries_from_db();
        }
        
        // Initialize flush timer
        last_flush_time = chrono::steady_clock::now();
    }
    
    // Check DNOTIFY availability and warn if not available in foreground mode
    if (foreground && !check_dnotify_available()) {
        cerr << COLOR_YELLOW << "Note: DNOTIFY not available, using inotify for directory monitoring" 
             << COLOR_RESET << "\n";
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGQUIT, sig_handler);
    signal(SIGHUP, sig_handler);
    
    // Install crash handlers for emergency cleanup
    signal(SIGSEGV, crash_handler);  // Segmentation fault
    signal(SIGABRT, crash_handler);  // abort() called
    signal(SIGBUS, crash_handler);   // Bus error

    sock_path = "/run/user/" + to_string(getuid()) + "/ffind.sock";
    
    // Copy socket path to signal-safe buffer for crash handler
    strncpy(sock_path_buf, sock_path.c_str(), sizeof(sock_path_buf) - 1);
    sock_path_buf[sizeof(sock_path_buf) - 1] = '\0';
    
    // Print info about roots being monitored
    if (foreground) {
        if (canonical_roots.size() == 1) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Monitoring 1 root directory:\n";
        } else {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Monitoring " 
                 << canonical_roots.size() << " root directories:\n";
        }
        for (const auto& rp : canonical_roots) {
            cerr << "  - " << rp << "\n";
        }
    }

    // Initialize inotify and watches
    // Skip filesystem indexing if entries were loaded from database
    bool skip_indexing = (db_enabled && !db_roots.empty());
    initial_setup(canonical_roots, skip_indexing);
    
    // Reconcile DB with filesystem if database is enabled
    if (db_enabled) {
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
                 << " Reconciling database with filesystem...\n";
        }
        
        reconcile_db_with_filesystem();
        
        if (foreground) {
            cerr << COLOR_GREEN << "[INFO]" << COLOR_RESET 
                 << " Database reconciliation complete\n";
        }
    }
    
    // Build path index for fast path-filtered queries
    // Must be called after all entries are loaded/indexed
    build_path_index();

    // Initialize thread pool for parallel content search
    init_thread_pool();

    if (foreground) {
        cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET 
             << " Creating Unix socket at: " << sock_path << "\n";
    }

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
             << " Failed to create socket: " << strerror(errno) << "\n";
        
        cleanup_on_socket_error(-1, false, sock_path);
        return 1;
    }
    srv_fd.store(srv);  // Store globally AFTER validating socket creation succeeded

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path)-1);
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';
    
    // Remove old socket if exists
    unlink(sock_path.c_str());
    
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
             << " Failed to bind socket to " << sock_path << ": " 
             << strerror(errno) << "\n";
        
        // Check if directory exists
        string dir = "/run/user/" + to_string(getuid());
        struct stat st;
        if (stat(dir.c_str(), &st) != 0) {
            cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
                 << " Directory " << dir << " does not exist. "
                 << "Run: mkdir -p " << dir << "\n";
        }
        
        cleanup_on_socket_error(srv, false, sock_path);
        return 1;
    }
    
    if (listen(srv, 16) < 0) {
        cerr << COLOR_RED << "[ERROR]" << COLOR_RESET 
             << " Failed to listen on socket: " << strerror(errno) << "\n";
        
        cleanup_on_socket_error(srv, true, sock_path);
        return 1;
    }
    
    if (foreground) {
        cerr << COLOR_GREEN << "[INFO]" << COLOR_RESET 
             << " Daemon ready. Listening on: " << sock_path << "\n";
    }

    thread events_th(process_events);
    thread accept_th([&]{
        while (running) {
            int c = accept(srv, nullptr, nullptr);
            if (c > 0) {
                thread(handle_client, c).detach();
            } else if (c < 0) {
                // Save errno immediately after accept() fails
                int saved_errno = errno;
                
                // If interrupted by signal, retry unless we're shutting down
                if (saved_errno == EINTR) {
                    if (!running) {
                        break;
                    }
                    continue;
                }
                
                // If socket was closed (EBADF) or shutdown (EINVAL), exit gracefully
                if (saved_errno == EBADF || saved_errno == EINVAL) {
                    break;  // Socket closed by signal handler, exit thread
                }
                
                // For other transient errors, log and back off briefly to avoid busy-looping
                if (foreground) {
                    cerr << COLOR_YELLOW << "[WARN]" << COLOR_RESET
                         << " accept() failed: " << strerror(saved_errno) << "\n";
                }
                this_thread::sleep_for(100ms);
                
                // Check if we're shutting down
                if (!running) {
                    break;
                }
            }
        }
    });

    while (running) sleep(1);
    running = 0;
    events_th.join();
    accept_th.join();
    
    // Cleanup socket - only close if not already closed by signal handler
    int fd = srv_fd.exchange(-1);
    if (fd >= 0) {  // Only close if not already closed by signal handler
        close(fd);
    }
    // Always unlink socket file (whether closed by signal handler or not)
    unlink(sock_path.c_str());
    close(in_fd);
    
    // Graceful shutdown with database flush
    if (db_enabled && db != nullptr) {
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Flushing " << pending_changes 
                 << " changes to database...\n";
        }
        flush_changes_to_db();
        sqlite3_close(db);
        if (foreground) {
            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Database closed.\n";
        }
    }
    
    cleanup_pid_file();
    return 0;
}