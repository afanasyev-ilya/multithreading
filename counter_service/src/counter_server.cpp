#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal> // For signal handling
#include <unistd.h> // For sleep (optional, for demonstration)


struct Settings {
    int port {};
    bool verbose {};
};


void signal_handler(int signum) {
    if (signum == SIGINT) {
        std::cout << "\nCtrl+C detected. Initiating graceful shutdown..." << std::endl;
        throw std::string("external shut down");
    }
}


struct ServerData {
    std::unordered_map<std::string, int> post_counters;
};

class Server {
    int server_fd;
    const int q_size = 512;

    ServerData data;

    void connect_to_socket(int port) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::string("Can not create socket");
        }

        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            throw std::string("setsockopt (SO_REUSEADDR) failed");
        }
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt))) {
            throw std::string("setsockopt (SO_REUSEPORT) failed");
        }

        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port); // Host to network short

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            throw std::string("Can't bind to port");
        }

        std::cout << "Server bound to port " << port << std::endl;

        if (listen(server_fd, q_size) < 0) { // 512 is the backlog queue size
            throw std::string("can't listen on socket");
        }

        std::cout << "listenning to connections... " << std::endl;
    }

    void close_socket() {
        close(server_fd);
    }

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

    void reply_client(int client_socket, const std::string &reply) {
        send(client_socket, reply.c_str(), reply.size(), 0);
    }

    void handle_client(int client_socket, std::thread::id tid) {
        std::string inbuf;
        std::string command;

        while(true) {
            if (!read_line(client_socket, command, inbuf)) 
                break;
            if (command.empty()) 
                continue;

            std::istringstream iss(command);
            std::string cmd_name; 
            iss >> cmd_name;

            std::cout << "tid " << tid << " recieved command " << cmd_name << std::endl;
        
            if(cmd_name == "QUIT") {
                reply_client(client_socket, "BYE\n");
            } else if (cmd_name == "STATS") {
                std::ostringstream os;    
                os << "STATS post counters=" << data.post_counters.size() << "\n";
                reply_client(client_socket, os.str());
            } else if (cmd_name == "INC") {
                std::string key;
                iss >> key;

                int change = 1;
                iss >> change;

                data.post_counters[key] += change;
                reply_client(client_socket, "OK\n");
            } else if (cmd_name == "GET") {
                std::string key;
                iss >> key;

                std::string reply;
                auto it = data.post_counters.find(key);
                if(it != data.post_counters.end()) {
                    reply = "VALUE = " + std::to_string(it->second) + "\n";
                } else {
                    reply = "key not found\n";
                }

                reply_client(client_socket, reply);
            } else {
                continue;
            }
        }
    }
public:
    Server(int port) {
        // for now outside of class
        std::signal(SIGINT, signal_handler);

        connect_to_socket(port);
    }

    ~Server() {
        close_socket();
    }

    void thread_worker(bool verbose) {
        std::thread::id tid = std::this_thread::get_id();
        std::cout << "Server thread created, Thread ID: " << tid << std::endl;

        while(true) {
            // waiting for clients connections
            sockaddr_in cli{}; socklen_t clilen = sizeof(cli);
    
            int client_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&cli), &clilen);

            if (client_socket < 0) {
                throw std::string("Problem with listening to socket");
            }

            if (verbose) {
                char ip[INET_ADDRSTRLEN]{};
                ::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                std::cerr << "Accepted connection from " << ip << ":" << ntohs(cli.sin_port) << "\n";
            }

            handle_client(client_socket, tid);
            close(client_socket);
        }
    }

    void run(int num_threads, bool verbose) {
        std::vector<std::thread> threads;

        for(int i = 0; i < num_threads; i++) {
            threads.push_back(std::move(std::thread(&Server::thread_worker, this, verbose)));
        }

        for(auto &thread: threads) {
            thread.join();
        }

        std::cout << "Master thread finished" << std::endl;
    }
};

int main() {
    try {
        const int num_threads = 4;

        Settings settings = {.port = 9000, .verbose = true};

        Server server(settings.port);
        server.run(num_threads, settings.verbose);
    } catch (const std::string &error) {
        std::cout << "Got critical error " << error << ", aborting..." << std::endl;
    }
    

    return 0;
}