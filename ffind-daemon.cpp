// ffind-daemon.cpp (latest full – with -path, -type, -size, -mtime, regex ±i, content fixed/regex)
#define _GNU_SOURCE
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
#include <regex>
#include <string.h>
#include <arpa/inet.h>

using namespace std;
using namespace std::filesystem;
using namespace std::chrono_literals;

struct Entry {
    string path;
    int64_t size = 0;
    time_t mtime = 0;
    bool is_dir = false;
};

vector<Entry> entries;
mutex mtx;
string root_path;
string sock_path;
string pid_file_path;
char pid_file_path_buf[256] = {0};  // Fixed buffer for signal-safe cleanup
volatile sig_atomic_t running = 1;
int in_fd = -1;
unordered_map<int, string> wd_to_dir;

// Directory rename tracking
unordered_map<uint32_t, pair<string, chrono::steady_clock::time_point>> pending_moves;
mutex pending_moves_mtx;
bool foreground = false;

// ANSI color codes for stderr output
const char* COLOR_RED = "\033[1;31m";
const char* COLOR_YELLOW = "\033[1;33m";
const char* COLOR_CYAN = "\033[36m";
const char* COLOR_BOLD = "\033[1m";
const char* COLOR_RESET = "\033[0m";

// Check if DNOTIFY is available on this system
bool check_dnotify_available() {
    // Try to use fcntl with F_NOTIFY on a test file descriptor
    // DNOTIFY requires DN_* constants which may not be defined on all systems
    #ifndef F_NOTIFY
    return false;
    #else
    // Create a temporary file descriptor to test
    int test_fd = open("/tmp", O_RDONLY);
    if (test_fd < 0) return false;
    
    // Try to set F_NOTIFY - if it fails with ENOSYS, DNOTIFY is not available
    int result = fcntl(test_fd, F_NOTIFY, 0);
    close(test_fd);
    
    // If errno is ENOSYS, kernel doesn't support DNOTIFY
    if (result < 0 && errno == ENOSYS) {
        return false;
    }
    
    // DNOTIFY might be available (but we're using inotify anyway)
    return true;
    #endif
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

void sig_handler(int) { 
    running = 0; 
    cleanup_pid_file();
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

void update_or_add(const string& full) {
    struct stat st {};
    if (lstat(full.c_str(), &st) != 0) return;

    bool is_dir = S_ISDIR(st.st_mode);
    int64_t sz = is_dir ? 0 : st.st_size;

    lock_guard<mutex> lk(mtx);
    auto it = find_if(entries.begin(), entries.end(), [&](const Entry& e){ return e.path == full; });
    if (it != entries.end()) {
        it->size = sz;
        it->mtime = st.st_mtime;
        it->is_dir = is_dir;
    } else {
        entries.emplace_back(full, sz, st.st_mtime, is_dir);
    }
}

void remove_path(const string& full, bool recursive = false) {
    lock_guard<mutex> lk(mtx);
    if (recursive) {
        entries.erase(remove_if(entries.begin(), entries.end(), [&](const Entry& e){
            return e.path == full || e.path.starts_with(full + "/");
        }), entries.end());
    } else {
        entries.erase(remove_if(entries.begin(), entries.end(), [&](const Entry& e){ return e.path == full; }), entries.end());
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

void add_watch(const string& dir) {
    int wd = inotify_add_watch(in_fd, dir.c_str(),
        IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd > 0) wd_to_dir[wd] = dir;
}

void add_directory_recursive(const string& dir) {
    // Add the directory itself
    update_or_add(dir);
    add_watch(dir);
    
    // Recursively add all subdirectories and files
    try {
        for (auto& e : directory_iterator(dir)) {
            string p = e.path().string();
            if (e.is_directory()) {
                add_directory_recursive(p);
            } else {
                update_or_add(p);
            }
        }
    } catch (...) {}
}

void initial_setup(const string& r) {
    root_path = canonical(r).string();
    if (root_path.back() != '/') root_path += '/';

    in_fd = inotify_init1(IN_NONBLOCK);
    assert(in_fd > 0);

    {
        lock_guard<mutex> lk(mtx);
        for (auto& e : recursive_directory_iterator(root_path, directory_options::skip_permission_denied)) {
            try {
                string p = e.path().string();
                struct stat st {};
                if (lstat(p.c_str(), &st) == 0) {
                    bool is_dir = S_ISDIR(st.st_mode);
                    entries.emplace_back(p, is_dir ? 0LL : st.st_size, st.st_mtime, is_dir);
                }
            } catch (...) {}
        }
    }

    function<void(const string&)> rec_add = [&](const string& d) {
        add_watch(d);
        try {
            for (auto& e : directory_iterator(d)) {
                if (e.is_directory()) rec_add(e.path().string());
            }
        } catch (...) {}
    };
    rec_add(root_path);
}

void process_events() {
    char buf[8192] __attribute__((aligned(8)));
    while (running) {
        // Periodically clean up stale pending moves
        cleanup_stale_pending_moves();
        
        ssize_t len = read(in_fd, buf, sizeof(buf));
        if (len > 0) {
            char* ptr = buf;
            while (ptr < buf + len) {
                auto* ev = (struct inotify_event*)ptr;
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
                    remove_path(dir, true);
                    if (foreground) {
                        int removed_count = 0;
                        {
                            lock_guard<mutex> lk(mtx);
                            for (const auto& e : entries) {
                                if (e.path == dir || e.path.starts_with(dir + "/")) {
                                    removed_count++;
                                }
                            }
                        }
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
                if (isd) {
                    if (ev->mask & IN_CREATE) {
                        add_directory_recursive(full);
                        if (foreground) {
                            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory created: "
                                 << COLOR_BOLD << full << COLOR_RESET << " (watch added)\n";
                        }
                    }
                    if (ev->mask & IN_MOVED_FROM) {
                        // Track this move with its cookie
                        lock_guard<mutex> lk(pending_moves_mtx);
                        pending_moves[ev->cookie] = {full, chrono::steady_clock::now()};
                        // Don't remove yet - wait to see if there's a matching MOVED_TO
                    }
                    if (ev->mask & IN_MOVED_TO) {
                        // Check if there's a matching MOVED_FROM with same cookie
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
                            handle_directory_rename(old_path, full);
                            // Re-add watch for the new location
                            add_watch(full);
                        } else {
                            // Moved into tree from outside
                            add_directory_recursive(full);
                            if (foreground) {
                                cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory created: "
                                     << COLOR_BOLD << full << COLOR_RESET << " (moved in, watch added)\n";
                            }
                        }
                    }
                    if (ev->mask & IN_DELETE) {
                        remove_path(full, true);
                        if (foreground) {
                            cerr << COLOR_CYAN << "[INFO]" << COLOR_RESET << " Directory deleted: "
                                 << COLOR_BOLD << full << COLOR_RESET << " (watch removed)\n";
                        }
                    }
                } else {
                    if (ev->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY | IN_CLOSE_WRITE)) update_or_add(full);
                    if (ev->mask & (IN_DELETE | IN_MOVED_FROM)) remove_path(full);
                }
            }
        } else if (len < 0 && errno != EAGAIN) break;
        else this_thread::sleep_for(50ms);
    }
}

void handle_client(int fd) {
    uint32_t net_nlen, net_plen, net_clen;
    if (read(fd, &net_nlen, 4) != 4) { close(fd); return; }
    uint32_t name_len = ntohl(net_nlen);
    string name_pat(name_len, '\0');
    if (read(fd, name_pat.data(), name_len) != (ssize_t)name_len) { close(fd); return; }

    if (read(fd, &net_plen, 4) != 4) { close(fd); return; }
    uint32_t path_len = ntohl(net_plen);
    string path_pat(path_len, '\0');
    if (read(fd, path_pat.data(), path_len) != (ssize_t)path_len) { close(fd); return; }

    if (read(fd, &net_clen, 4) != 4) { close(fd); return; }
    uint32_t content_len = ntohl(net_clen);
    string content_pat(content_len, '\0');
    if (read(fd, content_pat.data(), content_len) != (ssize_t)content_len) { close(fd); return; }

    uint8_t flags = 0;
    if (read(fd, &flags, 1) != 1) flags = 0;
    bool case_ins = flags & 1;      // bit 0 (value 1)
    bool is_regex = flags & 2;      // bit 1 (value 2)
    bool content_glob = flags & 4;  // bit 2 (value 4)

    uint8_t type_filter = 0;
    if (read(fd, &type_filter, 1) != 1) type_filter = 0;

    uint8_t size_op = 0;
    int64_t size_val = 0;
    if (read(fd, &size_op, 1) == 1 && size_op) read(fd, &size_val, 8);

    uint8_t mtime_op = 0;
    int32_t mtime_days = 0;
    if (read(fd, &mtime_op, 1) == 1 && mtime_op) read(fd, &mtime_days, 4);

    // Read context line parameters (added in v1.1 for context lines feature)
    // Note: For backward compatibility with older daemons, these bytes default to 0 if read fails.
    // This protocol extension approach is acceptable for new features where both client and daemon
    // are updated together. Old clients connecting to new daemons will work (daemon reads 0s).
    // New clients connecting to old daemons may have issues - this is expected for feature updates.
    uint8_t before_ctx = 0, after_ctx = 0;
    if (read(fd, &before_ctx, 1) != 1) before_ctx = 0;
    if (read(fd, &after_ctx, 1) != 1) after_ctx = 0;

    bool has_content = !content_pat.empty();

    unique_ptr<regex> re;
    if (has_content && is_regex) {
        regex_constants::syntax_option_type re_flags = regex_constants::ECMAScript;
        if (case_ins) re_flags |= regex_constants::icase;
        try {
            re = make_unique<regex>(content_pat, re_flags);
        } catch (const regex_error&) {
            string err = "Invalid regex pattern\n";
            write(fd, err.c_str(), err.size());
            close(fd);
            return;
        }
    }

    int fnm_flags = case_ins ? FNM_CASEFOLD : 0;

    lock_guard<mutex> lk(mtx);

    vector<const Entry*> candidates;
    for (const auto& e : entries) {
        bool type_match = (type_filter == 0) ||
                          (type_filter == 1 && !e.is_dir) ||
                          (type_filter == 2 && e.is_dir);
        if (!type_match) continue;
        if (e.is_dir && has_content) continue;

        if (size_op) {
            bool match = false;
            if (size_op == 1) match = e.size < size_val;
            else if (size_op == 2) match = e.size == size_val;
            else if (size_op == 3) match = e.size > size_val;
            if (!match) continue;
        }

        if (mtime_op) {
            time_t now = time(nullptr);
            int32_t days_old = (now - e.mtime) / 86400;
            bool match = false;
            if (mtime_op == 1) match = days_old < mtime_days;
            else if (mtime_op == 2) match = days_old == mtime_days;
            else if (mtime_op == 3) match = days_old > mtime_days;
            if (!match) continue;
        }

        string rel = e.path.substr(root_path.size());

        size_t pos = e.path.rfind('/');
        string_view base = (pos == string::npos) ? string_view(e.path) : string_view(e.path.data() + pos + 1);

        bool name_match = fnmatch(name_pat.c_str(), base.data(), fnm_flags) == 0;
        bool path_match = path_pat.empty() || fnmatch(path_pat.c_str(), rel.c_str(), fnm_flags) == 0;

        if (name_match && path_match) {
            if (!has_content) {
                string line = e.path + "\n";
                write(fd, line.c_str(), line.size());
            } else {
                candidates.push_back(&e);
            }
        }
    }

    if (has_content) {
        for (const auto* ep : candidates) {
            const string& path = ep->path;
            ifstream ifs(path, ios::binary);
            if (!ifs) continue;

            streampos fsize = ifs.tellg();
            ifs.seekg(0, ios::end);
            fsize = ifs.tellg() - fsize;
            ifs.seekg(0);

            size_t check = min<size_t>(1024, max<streampos>(0, fsize));
            vector<char> head(check);
            bool binary = false;
            if (check > 0) {
                ifs.read(head.data(), check);
                for (char c : head) if (c == '\0') { binary = true; break; }
                ifs.seekg(0);
            }
            if (binary) continue;

            if (before_ctx == 0 && after_ctx == 0) {
                // No context - original behavior
                string line;
                size_t lineno = 1;
                while (getline(ifs, line)) {
                    bool match = false;
                    if (content_glob) {
                        // Use fnmatch for glob pattern matching
                        int fnm_flags_content = case_ins ? FNM_CASEFOLD : 0;
                        match = fnmatch(content_pat.c_str(), line.c_str(), fnm_flags_content) == 0;
                    } else if (is_regex) {
                        match = regex_search(line, *re);
                    } else if (case_ins) {
                        match = strcasestr(line.c_str(), content_pat.c_str()) != nullptr;
                    } else {
                        match = line.find(content_pat) != string::npos;
                    }
                    if (match) {
                        string out = path + ":" + to_string(lineno) + ":" + line + "\n";
                        write(fd, out.c_str(), out.size());
                    }
                    ++lineno;
                }
            } else {
                // With context lines
                vector<pair<size_t, string>> all_lines; // lineno, content
                string line;
                size_t lineno = 1;
                
                // Read all lines into memory
                while (getline(ifs, line)) {
                    all_lines.emplace_back(lineno, line);
                    ++lineno;
                }
                
                // Find all matching line indices and store in a set for O(1) lookup
                vector<size_t> match_indices;
                unordered_set<size_t> match_set;
                for (size_t i = 0; i < all_lines.size(); ++i) {
                    bool match = false;
                    const string& content = all_lines[i].second;
                    if (content_glob) {
                        // Use fnmatch for glob pattern matching
                        int fnm_flags_content = case_ins ? FNM_CASEFOLD : 0;
                        match = fnmatch(content_pat.c_str(), content.c_str(), fnm_flags_content) == 0;
                    } else if (is_regex) {
                        match = regex_search(content, *re);
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
                    
                    // Output ranges with separator between non-contiguous groups
                    for (size_t r = 0; r < ranges.size(); ++r) {
                        if (r > 0) {
                            string sep = "--\n";
                            write(fd, sep.c_str(), sep.size());
                        }
                        
                        for (size_t i = ranges[r].first; i <= ranges[r].second; ++i) {
                            // Check if this line is a match line using O(1) lookup
                            bool is_match = match_set.count(i) > 0;
                            char separator = is_match ? ':' : '-';
                            
                            string out = path + ":" + to_string(all_lines[i].first) + separator + all_lines[i].second + "\n";
                            write(fd, out.c_str(), out.size());
                        }
                    }
                }
            }
        }
    }

    close(fd);
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        cerr << "Usage: ffind-daemon [--foreground] /path/to/root\n";
        return 1;
    }

    bool fg = false;
    string root;
    if (argc == 3) {
        if (string(argv[1]) != "--foreground") {
            cerr << "Unknown option\n";
            return 1;
        }
        fg = true;
        root = argv[2];
    } else {
        root = argv[1];
    }
    
    // Set global foreground flag
    foreground = fg;

    // Determine PID file path before daemonizing
    pid_file_path = get_pid_file_path();
    
    // Copy to fixed buffer for signal-safe cleanup
    strncpy(pid_file_path_buf, pid_file_path.c_str(), sizeof(pid_file_path_buf) - 1);
    pid_file_path_buf[sizeof(pid_file_path_buf) - 1] = '\0';

    if (!foreground) daemonize();

    // Check and create PID file after daemonizing (so we get the correct PID)
    // This function handles race conditions atomically
    if (!check_and_create_pid_file(pid_file_path, foreground)) {
        return 1;
    }
    
    // Check DNOTIFY availability and warn if not available in foreground mode
    if (foreground && !check_dnotify_available()) {
        cerr << COLOR_YELLOW << "Note: DNOTIFY not available, using inotify for directory monitoring" 
             << COLOR_RESET << "\n";
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    sock_path = "/run/user/" + to_string(getuid()) + "/ffind.sock";

    initial_setup(root);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path)-1);
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';
    unlink(sock_path.c_str());
    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 16);

    thread events_th(process_events);
    thread accept_th([&]{
        while (running) {
            int c = accept(srv, nullptr, nullptr);
            if (c > 0) thread(handle_client, c).detach();
        }
    });

    while (running) sleep(1);
    running = 0;
    events_th.join();
    accept_th.join();
    close(srv);
    unlink(sock_path.c_str());
    close(in_fd);
    cleanup_pid_file();
    return 0;
}