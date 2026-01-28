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
             << "  ffind -g \"TODO*\" -i\n"
             << "  ffind \"*.cpp\" --color=always\n";
        return 1;
    }

    string name_pat = "*";
    string path_pat = "";
    string content_pat = "";
    string content_glob = "";
    bool case_ins = false;
    bool is_regex = false;
    uint8_t type_filter = 0;
    uint8_t size_op = 0;
    int64_t size_val = 0;
    uint8_t mtime_op = 0;
    int32_t mtime_days = 0;
    ColorMode color_mode = ColorMode::AUTO;
    uint8_t before_ctx = 0;
    uint8_t after_ctx = 0;

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
            } else if (arg == "-g") {
                if (++i >= argc) { cerr << "Missing -g pattern\n"; return 1; }
                content_glob = argv[i];
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
            } else if (arg == "-A") {
                if (++i >= argc) { cerr << "Missing -A arg\n"; return 1; }
                try {
                    int val = stoi(argv[i]);
                    if (val < 0 || val > 255) { cerr << "-A must be 0-255\n"; return 1; }
                    after_ctx = static_cast<uint8_t>(val);
                } catch (const invalid_argument&) {
                    cerr << "-A requires a valid integer\n"; return 1;
                } catch (const out_of_range&) {
                    cerr << "-A value out of range\n"; return 1;
                }
            } else if (arg == "-B") {
                if (++i >= argc) { cerr << "Missing -B arg\n"; return 1; }
                try {
                    int val = stoi(argv[i]);
                    if (val < 0 || val > 255) { cerr << "-B must be 0-255\n"; return 1; }
                    before_ctx = static_cast<uint8_t>(val);
                } catch (const invalid_argument&) {
                    cerr << "-B requires a valid integer\n"; return 1;
                } catch (const out_of_range&) {
                    cerr << "-B value out of range\n"; return 1;
                }
            } else if (arg == "-C") {
                if (++i >= argc) { cerr << "Missing -C arg\n"; return 1; }
                try {
                    int val = stoi(argv[i]);
                    if (val < 0 || val > 255) { cerr << "-C must be 0-255\n"; return 1; }
                    before_ctx = after_ctx = static_cast<uint8_t>(val);
                } catch (const invalid_argument&) {
                    cerr << "-C requires a valid integer\n"; return 1;
                } catch (const out_of_range&) {
                    cerr << "-C value out of range\n"; return 1;
                }
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

    if (!content_glob.empty() && !content_pat.empty()) {
        cerr << "Cannot use -g with -c\n";
        return 1;
    }

    if (!content_glob.empty() && is_regex) {
        cerr << "Cannot use -g with -r\n";
        return 1;
    }

    if (is_regex && content_pat.empty()) {
        cerr << "-r needs -c\n";
        return 1;
    }

    if ((before_ctx > 0 || after_ctx > 0) && content_pat.empty() && content_glob.empty()) {
        cerr << "Context lines (-A/-B/-C) need -c or -g\n";
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

    // Send content_glob as content_pat if -g is used
    string content_to_send = content_glob.empty() ? content_pat : content_glob;
    uint32_t net_clen = htonl(content_to_send.size());
    write(c, &net_clen, 4);
    write(c, content_to_send.data(), content_to_send.size());

    uint8_t flags = 0;
    if (case_ins) flags |= 1;
    if (is_regex) flags |= 2;
    if (!content_glob.empty()) flags |= 4; // bit 2 (value 4) for content_glob
    write(c, &flags, 1);

    write(c, &type_filter, 1);

    write(c, &size_op, 1);
    if (size_op) write(c, &size_val, 8);

    write(c, &mtime_op, 1);
    if (mtime_op) write(c, &mtime_days, 4);

    // Send context line parameters (protocol extension for context lines feature)
    // Note: Old daemons will ignore/misinterpret these bytes, but this is expected
    // for new feature additions. Users should update both client and daemon together.
    write(c, &before_ctx, 1);
    write(c, &after_ctx, 1);

    // Read and colorize output incrementally
    bool has_content = !content_pat.empty() || !content_glob.empty();
    
    // Prepare regex for content matching if needed
    unique_ptr<regex> re_matcher;
    if (!content_pat.empty() && is_regex) {
        regex_constants::syntax_option_type re_flags = regex_constants::ECMAScript;
        if (case_ins) re_flags |= regex_constants::icase;
        try {
            re_matcher = make_unique<regex>(content_pat, re_flags);
        } catch (const regex_error& e) {
            cerr << "Invalid regex pattern: " << e.what() << "\n";
            return 1;
        }
    }
    
    // Process output line by line as it arrives (streaming)
    string line_buffer;
    char buf[8192];
    ssize_t n;
    
    auto process_line = [&](const string& line) {
        if (line.empty()) return;
        
        // Check for separator line
        if (line == "--") {
            cout << "--\n";
            return;
        }
        
        if (!has_content) {
            // Simple path output - color with bold
            cout << BOLD << line << RESET << "\n";
        } else {
            // Content search: path:lineno:content or path:lineno-content format
            size_t first_colon = line.find(':');
            if (first_colon == string::npos) {
                // Fallback: no colon found, just print
                cout << line << "\n";
                return;
            }
            
            size_t second_sep = line.find(':', first_colon + 1);
            bool is_context_line = false;
            if (second_sep == string::npos) {
                // Try to find dash separator (for context lines)
                second_sep = line.find('-', first_colon + 1);
                if (second_sep != string::npos) {
                    is_context_line = true;
                }
            }
            
            if (second_sep == string::npos) {
                // Fallback: only one separator, just print
                cout << line << "\n";
                return;
            }
            
            // Validate that the substring between first_colon and second_sep is a valid line number
            string lineno = line.substr(first_colon + 1, second_sep - first_colon - 1);
            bool valid_lineno = !lineno.empty() && all_of(lineno.begin(), lineno.end(), ::isdigit);
            if (!valid_lineno) {
                // Not a valid line number, just print as-is
                cout << line << "\n";
                return;
            }
            
            string path = line.substr(0, first_colon);
            string content = line.substr(second_sep + 1);
            
            // Color the path (bold) and line number (cyan)
            cout << BOLD << path << RESET << ":" 
                 << CYAN << lineno << RESET << (is_context_line ? "-" : ":");
            
            // Highlight matching content only for match lines (not context lines)
            if (use_colors && !is_context_line) {
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
                } else if (!content_glob.empty()) {
                    // For glob patterns, we don't highlight since fnmatch doesn't give position
                    // Just print the content as-is
                    found_match = false;
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
    };
    
    // Stream processing: read chunks and process complete lines immediately
    while ((n = read(c, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                // Complete line found - process it immediately
                process_line(line_buffer);
                line_buffer.clear();
            } else {
                line_buffer += buf[i];
            }
        }
    }
    
    // Process any remaining partial line
    if (!line_buffer.empty()) {
        process_line(line_buffer);
    }
    
    close(c);
    return 0;
}