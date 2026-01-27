// ffind.cpp (latest full client)
#include <bits/stdc++.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "Usage examples:\n"
             << "  ffind \"*.cpp\"\n"
             << "  ffind -path \"src/*\" -type f\n"
             << "  ffind -size +1G -mtime -7\n"
             << "  ffind -c \"todo\" -r -i\n";
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

    char buf[8192];
    ssize_t n;
    while ((n = read(c, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
    }
    close(c);
    return 0;
}