#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

struct Args {
    std::string host = "127.0.0.1";
    int port = 9000;
    int seconds = 5;
    int keys = 10000;
    int write_pct = 50; // 0..100
    int seed = 42;
    bool verbose = false;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--host" && i + 1 < argc) a.host = argv[++i];
        else if (s == "--port" && i + 1 < argc) a.port = std::stoi(argv[++i]);
        else if (s == "--secs" && i + 1 < argc) a.seconds = std::stoi(argv[++i]);
        else if (s == "--keys" && i + 1 < argc) a.keys = std::stoi(argv[++i]);
        else if (s == "--writes" && i + 1 < argc) a.write_pct = std::stoi(argv[++i]);
        else if (s == "--seed" && i + 1 < argc) a.seed = std::stoi(argv[++i]);
        else if (s == "--verbose") a.verbose = true;
        else if (s == "-h" || s == "--help") {
            std::cout << "Usage: counter_client [--host H] [--port P] [--secs S] [--keys N] [--writes PCT] [--seed X]\n";
            std::exit(0);
        }
    }
    if (a.write_pct < 0) a.write_pct = 0; if (a.write_pct > 100) a.write_pct = 100;
    return a;
}

int connect_tcp(const std::string& host, int port) {
    struct addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rc) << "\n"; return -1;
    }
    int fd = -1;
    for (auto p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) perror("connect");
    return fd;
}

bool write_all(int fd, const std::string& s) {
    const char* p = s.data(); size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::send(fd, p, left, 0);
        if (n < 0) { if (errno == EINTR) continue; perror("send"); return false; }
        left -= (size_t)n; p += n;
    }
    return true;
}

bool read_line(int fd, std::string& out, std::string& buf) {
    while (true) {
        auto pos = buf.find('\n');
        if (pos != std::string::npos) { out = buf.substr(0, pos); buf.erase(0, pos + 1); return true; }
        char tmp[4096];
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n == 0) return false;
        if (n < 0) { if (errno == EINTR) continue; perror("recv"); return false; }
        buf.append(tmp, tmp + n);
    }
}

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);
    int fd = connect_tcp(args.host, args.port);
    if (fd < 0) return 1;

    std::mt19937 rng(args.seed);
    std::uniform_int_distribution<int> keydist(0, args.keys - 1);
    std::uniform_int_distribution<int> pct(0, 99);

    uint64_t ops = 0, reads = 0, writes = 0;
    auto t0 = std::chrono::steady_clock::now();
    std::string rdbuf, line;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - t0).count();
        if (elapsed >= args.seconds) break;

        int k = keydist(rng);
        std::string key = "key" + std::to_string(k);
        bool do_write = (pct(rng) < args.write_pct);

        std::string cmd;
        if (do_write) { cmd = "INCR " + key + " 1\n"; }
        else { cmd = "GET " + key + "\n"; }

        if (!write_all(fd, cmd)) break;
        if (!read_line(fd, line, rdbuf)) break;
        // Optional sanity: check prefix
        if (do_write) { if (line.rfind("OK ", 0) == 0) ++writes; }
        else { if (line.rfind("VALUE ", 0) == 0) ++reads; }
        ++ops;
    }

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    double qps = secs > 0 ? ops / secs : 0.0;

    // Ask server for a quick stat & close
    write_all(fd, std::string("STATS\n"));
    if (read_line(fd, line, rdbuf)) {
        std::cout << line << "\n"; // prints: STATS ops=... uptime_s=... keys=...
    }
    write_all(fd, std::string("QUIT\n"));

    ::close(fd);

    std::cout << "Client run finished: ops=" << ops << ", reads=" << reads << ", writes=" << writes
              << ", secs=" << secs << ", qps=" << qps << "\n";
    return 0;
}
