#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

namespace {
volatile std::sig_atomic_t g_stop = 0;
void sigint_handler(int) { g_stop = 1; }

struct Args {
    int port = 9000;
    bool verbose = false;
    bool reuseport = true; // enabled by default
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--port" && i + 1 < argc) a.port = std::stoi(argv[++i]);
        else if (s == "--no-reuseport") a.reuseport = false;
        else if (s == "--verbose") a.verbose = true;
        else if (s == "-h" || s == "--help") {
            std::cout << "Usage: counter_server [--port P] [--no-reuseport] [--verbose]\n";
            std::exit(0);
        }
    }
    return a;
}

int create_listen_socket(int port, bool reuseport, bool verbose) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        ::close(fd); return -1;
    }
#ifdef SO_REUSEPORT
    if (reuseport) {
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
            perror("setsockopt SO_REUSEPORT");
            std::cerr << "Hint: SO_REUSEPORT requires Linux >= 3.9 and kernel support.\n";
            ::close(fd); return -1;
        }
        if (verbose) std::cerr << "SO_REUSEPORT enabled." << std::endl;
    }
#else
    (void)reuseport;
    if (verbose) std::cerr << "SO_REUSEPORT not available on this platform." << std::endl;
#endif

    // Disable Nagle to slightly reduce latency for small replies.
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        perror("setsockopt TCP_NODELAY");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(fd); return -1;
    }

    if (listen(fd, 512) < 0) {
        perror("listen");
        ::close(fd); return -1;
    }

    return fd;
}

// Simple buffered line reader for a connected socket. Returns false on EOF/error.
bool read_line(int fd, std::string& out, std::string& buf) {
    // Look for '\n' in existing buffer first.
    while (true) {
        auto pos = buf.find('\n');
        if (pos != std::string::npos) {
            out = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            return true;
        }
        char tmp[4096];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n == 0) return false; // peer closed
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("recv");
            return false;
        }
        buf.append(tmp, tmp + n);
    }
}

bool write_all(int fd, const std::string& s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::send(fd, p, left, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("send");
            return false;
        }
        left -= static_cast<size_t>(n);
        p += n;
    }
    return true;
}

struct ServerState {
    std::unordered_map<std::string, uint64_t> counters;
    uint64_t ops = 0; // processed commands
    std::chrono::steady_clock::time_point start_ts = std::chrono::steady_clock::now();
};

void handle_client(int cfd, ServerState& st, bool verbose) {
    std::string inbuf;
    std::string line;
    while (!g_stop) {
        if (!read_line(cfd, line, inbuf)) break; // EOF
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd; iss >> cmd;
        if (cmd == "INCR") {
            std::string key; iss >> key;
            long long delta = 1;
            if (!(iss >> delta)) delta = 1;
            uint64_t& v = st.counters[key];
            v += static_cast<uint64_t>(delta);
            ++st.ops;
            if (!write_all(cfd, "OK " + std::to_string(v) + "\n")) break;
        } else if (cmd == "GET") {
            std::string key; iss >> key;
            uint64_t v = 0;
            auto it = st.counters.find(key);
            if (it != st.counters.end()) v = it->second;
            ++st.ops;
            if (!write_all(cfd, "VALUE " + std::to_string(v) + "\n")) break;
        } else if (cmd == "STATS") {
            ++st.ops;
            auto now = std::chrono::steady_clock::now();
            double secs = std::chrono::duration<double>(now - st.start_ts).count();
            std::ostringstream os;
            os << "STATS ops=" << st.ops << " uptime_s=" << secs << " keys=" << st.counters.size() << "\n";
            if (!write_all(cfd, os.str())) break;
        } else if (cmd == "QUIT") {
            write_all(cfd, "BYE\n");
            break;
        } else {
            if (!write_all(cfd, "ERR unknown_command\n")) break;
        }
    }
    if (verbose) std::cerr << "Client disconnected." << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);
    auto args = parse_args(argc, argv);

    int lfd = create_listen_socket(args.port, args.reuseport, args.verbose);
    if (lfd < 0) return 1;

    std::cerr << "counter_server listening on 0.0.0.0:" << args.port
              << (args.reuseport ? " (SO_REUSEPORT on)" : "") << "\n";

    ServerState state;

    while (!g_stop) {
        sockaddr_in cli{}; socklen_t clilen = sizeof(cli);
        int cfd = ::accept(lfd, reinterpret_cast<sockaddr*>(&cli), &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        if (args.verbose) {
            char ip[INET_ADDRSTRLEN]{};
            ::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
            std::cerr << "Accepted connection from " << ip << ":" << ntohs(cli.sin_port) << "\n";
        }
        handle_client(cfd, state, args.verbose);
        ::close(cfd);
    }

    ::close(lfd);
    std::cerr << "Server exiting." << std::endl;
    return 0;
}