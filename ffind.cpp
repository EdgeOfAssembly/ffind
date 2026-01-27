// ffind.cpp (latest full client)
#include <bits/stdc++.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <regex>

using namespace std;

enum class ColorMode { NEVER, AUTO, ALWAYS };

// ANSI color codes
const char* RESET = "\033[0m";
const char* BOLD = "\033[1m";
const char* CYAN = "\033[36m";
const char* BOLD_RED = "\033[1;31m";

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "Usage examples:\n"
             << "  ffind \"*.cpp\"\n"
             << "  ffind -path \"src/*\" -type f\n"
             << "  ffind -size +1G -mtime -7\n"
             << "  ffind -c \"todo\" -r -i\n"
             << "  ffind \"*.cpp\" --color=always\n";
        return 1;
    }

    string name_pat = "*";
    string path_pat = "";
    string content_pat = "";
    bool case_ins = false;
    bool is_regex = false;
    uint8_t type_filter = 0;
    uint8_t size_op = 0;
    int64_t size_val = 0;
    uint8_t mtime_op = 0;
    int32_t mtime_days = 0;
    ColorMode color_mode = ColorMode::AUTO;

    bool has_dash = false;
    for (int i = 1; i < argc; ++i) if (argv[i][0] == '-') has_dash = true;

    if (!has_dash && argc == 2) {
        name_pat = argv[1];
    } else {
        int i = 1;
        while (i < argc) {
            string arg = argv[i];
            if (arg == "-c") {
                if (++i >= argc) { cerr << "Missing -c pattern\n"; return 1; }
                content_pat = argv[i];
            } else if (arg == "-name") {
                if (++i >= argc) { cerr << "Missing -name glob\n"; return 1; }
                name_pat = argv[i];
            } else if (arg == "-path") {
                if (++i >= argc) { cerr << "Missing -path glob\n"; return 1; }
                path_pat = argv[i];
            } else if (arg == "-type") {
                if (++i >= argc) { cerr << "Missing -type arg\n"; return 1; }
                string t = argv[i];
                if (t == "f") type_filter = 1;
                else if (t == "d") type_filter = 2;
                else { cerr << "-type f|d only\n"; return 1; }
            } else if (arg == "-size") {
                if (++i >= argc) { cerr << "Missing -size arg\n"; return 1; }
                string s = argv[i];
                char sign = 0;
                if (s[0] == '+' || s[0] == '-') { sign = s[0]; s = s.substr(1); }
                char unit = s.back();
                if (!isdigit(unit)) s.pop_back(); else unit = 'c';
                int64_t num = stoll(s);
                switch (unit) {
                    case 'c': break;
                    case 'b': num *= 512; break;
                    case 'k': num *= 1024; break;
                    case 'M': num *= 1024*1024; break;
                    case 'G': num *= 1024*1024*1024; break;
                    default: cerr << "Bad unit\n"; return 1;
                }
                size_op = sign == '+' ? 3 : sign == '-' ? 1 : 2;
                size_val = num;
            } else if (arg == "-mtime") {
                if (++i >= argc) { cerr << "Missing -mtime arg\n"; return 1; }
                string s = argv[i];
                char sign = 0;
                if (s[0] == '+' || s[0] == '-') { sign = s[0]; s = s.substr(1); }
                int32_t num = stoi(s);
                if (num < 0) { cerr << "-mtime positive\n"; return 1; }
                mtime_op = sign == '+' ? 3 : sign == '-' ? 1 : 2;
                mtime_days = num;
            } else if (arg == "-i") {
                case_ins = true;
            } else if (arg == "-r") {
                is_regex = true;
            } else if (arg.starts_with("--color=")) {
                string mode = arg.substr(8);
                if (mode == "never") color_mode = ColorMode::NEVER;
                else if (mode == "always") color_mode = ColorMode::ALWAYS;
                else if (mode == "auto") color_mode = ColorMode::AUTO;
                else { cerr << "--color must be auto/always/never\n"; return 1; }
            } else if (arg == "--color") {
                // Support --color as shorthand for --color=always
                color_mode = ColorMode::ALWAYS;
            } else if (arg[0] != '-') {
                // Positional argument - treat as name pattern
                name_pat = arg;
            } else {
                cerr << "Bad arg: " << arg << "\n";
                return 1;
            }
            ++i;
        }
    }

    if (is_regex && content_pat.empty()) {
        cerr << "-r needs -c\n";
        return 1;
    }

    // Determine if we should use colors
    bool use_colors = false;
    if (color_mode == ColorMode::ALWAYS) {
        use_colors = true;
    } else if (color_mode == ColorMode::AUTO) {
        use_colors = isatty(STDOUT_FILENO);
    }
    
    // Clear color codes if not using colors
    if (!use_colors) {
        RESET = "";
        BOLD = "";
        CYAN = "";
        BOLD_RED = "";
    }

    string sock_path = "/run/user/" + to_string(getuid()) + "/ffind.sock";

    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path)-1);
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';

    if (connect(c, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Daemon not running\n";
        return 1;
    }

    uint32_t net_nlen = htonl(name_pat.size());
    write(c, &net_nlen, 4);
    write(c, name_pat.data(), name_pat.size());

    uint32_t net_plen = htonl(path_pat.size());
    write(c, &net_plen, 4);
    write(c, path_pat.data(), path_pat.size());

    uint32_t net_clen = htonl(content_pat.size());
    write(c, &net_clen, 4);
    write(c, content_pat.data(), content_pat.size());

    uint8_t flags = 0;
    if (case_ins) flags |= 1;
    if (is_regex) flags |= 2;
    write(c, &flags, 1);

    write(c, &type_filter, 1);

    write(c, &size_op, 1);
    if (size_op) write(c, &size_val, 8);

    write(c, &mtime_op, 1);
    if (mtime_op) write(c, &mtime_days, 4);

    // Read and colorize output
    bool has_content = !content_pat.empty();
    string buffer;
    char buf[8192];
    ssize_t n;
    
    while ((n = read(c, buf, sizeof(buf))) > 0) {
        buffer.append(buf, n);
    }
    close(c);
    
    // Process buffer line by line
    istringstream stream(buffer);
    string line;
    
    // Prepare regex for content matching if needed
    unique_ptr<regex> re_matcher;
    if (has_content && is_regex) {
        regex_constants::syntax_option_type re_flags = regex_constants::ECMAScript;
        if (case_ins) re_flags |= regex_constants::icase;
        try {
            re_matcher = make_unique<regex>(content_pat, re_flags);
        } catch (const regex_error& e) {
            cerr << "Invalid regex pattern: " << e.what() << "\n";
            return 1;
        }
    }
    
    while (getline(stream, line)) {
        if (line.empty()) continue;
        
        if (!has_content) {
            // Simple path output - color with bold
            cout << BOLD << line << RESET << "\n";
        } else {
            // Content search: path:lineno:content format
            size_t first_colon = line.find(':');
            if (first_colon == string::npos) {
                // Fallback: no colon found, just print
                cout << line << "\n";
                continue;
            }
            
            size_t second_colon = line.find(':', first_colon + 1);
            if (second_colon == string::npos) {
                // Fallback: only one colon, just print
                cout << line << "\n";
                continue;
            }
            
            string path = line.substr(0, first_colon);
            string lineno = line.substr(first_colon + 1, second_colon - first_colon - 1);
            string content = line.substr(second_colon + 1);
            
            // Color the path (bold) and line number (cyan)
            cout << BOLD << path << RESET << ":" 
                 << CYAN << lineno << RESET << ":";
            
            // Highlight matching content
            if (use_colors) {
                bool found_match = false;
                size_t match_start = 0;
                size_t match_len = 0;
                
                if (is_regex && re_matcher) {
                    smatch match;
                    if (regex_search(content, match, *re_matcher)) {
                        match_start = match.position(0);
                        match_len = match.length(0);
                        found_match = true;
                    }
                } else if (case_ins) {
                    // Case-insensitive substring search
                    string lower_content = content;
                    string lower_pattern = content_pat;
                    transform(lower_content.begin(), lower_content.end(), lower_content.begin(), ::tolower);
                    transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
                    size_t pos = lower_content.find(lower_pattern);
                    if (pos != string::npos) {
                        match_start = pos;
                        match_len = content_pat.size();
                        found_match = true;
                    }
                } else {
                    // Case-sensitive substring search
                    size_t pos = content.find(content_pat);
                    if (pos != string::npos) {
                        match_start = pos;
                        match_len = content_pat.size();
                        found_match = true;
                    }
                }
                
                if (found_match) {
                    cout << content.substr(0, match_start)
                         << BOLD_RED << content.substr(match_start, match_len) << RESET
                         << content.substr(match_start + match_len) << "\n";
                } else {
                    cout << content << "\n";
                }
            } else {
                cout << content << "\n";
            }
        }
    }
    
    return 0;
}