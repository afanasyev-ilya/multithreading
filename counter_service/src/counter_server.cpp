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
#include <mutex>


struct Settings {
    int port {};
    bool verbose {};
    int num_threads {};
};


void signal_handler(int signum) {
    if (signum == SIGINT) {
        std::cout << "\nCtrl+C detected. Initiating graceful shutdown..." << std::endl;
        throw std::string("external shut down");
    }
}


size_t get_shard_index(const std::string& key, size_t num_shards) {
    return std::hash<std::string>{}(key) % num_shards;
}


struct ServerData {
    std::unordered_map<std::string, int> post_counters;
    std::mutex mtx;
};

class Server {
    int server_fd;
    const int q_size = 512;

    int num_shards;
    int num_threads;
    std::vector<ServerData> shards;

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

            // super verbose
            // std::cout << "tid " << tid << " recieved command " << cmd_name << std::endl;
        
            if(cmd_name == "QUIT") {
                reply_client(client_socket, "BYE\n");
            } else if (cmd_name == "STATS") {
                std::ostringstream os;
                
                int sum_size = 0;
                for(const auto &shard: shards) {
                    sum_size += shard.post_counters.size();
                }
                os << "STATS post counters=" << sum_size << "\n";
                reply_client(client_socket, os.str());
            } else if (cmd_name == "INC") {
                std::string key;
                iss >> key;

                int change = 1;
                iss >> change;

                // Hash key to determine which shard to use
                size_t shard_idx = get_shard_index(key, num_shards);
                {
                    std::lock_guard<std::mutex> lock(shards[shard_idx].mtx);
                    shards[shard_idx].post_counters[key] += change;
                }

                reply_client(client_socket, "OK\n");
            } else if (cmd_name == "GET") {
                std::string key;
                iss >> key;

                std::string reply;
                size_t shard_idx = get_shard_index(key, num_shards);
                {
                    std::lock_guard<std::mutex> lock(shards[shard_idx].mtx);
                    auto it = shards[shard_idx].post_counters.find(key);
                    if (it != shards[shard_idx].post_counters.end())
                        reply = "VALUE " + std::to_string(it->second) + "\n";
                    else
                        reply = "key not found\n";
                }

                reply_client(client_socket, reply);
            } else {
                continue;
            }
        }
    }
public:
    Server(int threads, int port): num_shards(threads), num_threads(threads), shards(threads) {
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

    void run(bool verbose) {
        std::vector<std::thread> threads;

        for(int i = 0; i < this->num_threads; i++) {
            threads.push_back(std::move(std::thread(&Server::thread_worker, this, verbose)));
        }

        for(auto &thread: threads) {
            thread.join();
        }

        std::cout << "Master thread finished" << std::endl;
    }
};

int load_cli_settings(Settings &settings, int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) { // Check if there's a next argument for the port number
                try {
                    settings.port = std::stoi(argv[++i]); // Convert string to integer and advance index
                } catch (const std::invalid_argument& e) {
                    std::cerr << "Error: Invalid port number provided." << std::endl;
                    return 1; // Exit with error
                } catch (const std::out_of_range& e) {
                    std::cerr << "Error: Port number out of range." << std::endl;
                    return 1; // Exit with error
                }
            } else {
                std::cerr << "Error: --port option requires an argument." << std::endl;
                return 1; // Exit with error
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) { // Check if there's a next argument for the port number
                try {
                    settings.num_threads = std::stoi(argv[++i]); // Convert string to integer and advance index
                } catch (const std::invalid_argument& e) {
                    std::cerr << "Error: Invalid port number provided." << std::endl;
                    return 1; // Exit with error
                } catch (const std::out_of_range& e) {
                    std::cerr << "Error: Port number out of range." << std::endl;
                    return 1; // Exit with error
                }
            } else {
                std::cerr << "Error: --port option requires an argument." << std::endl;
                return 1; // Exit with error
            }
        }
    }   
}

int main(int argc, char* argv[]) {
    try {
        Settings settings = {.port = 9000, .verbose = true, .num_threads = 4};

        load_cli_settings(settings, argc, argv);

        Server server(settings.num_threads, settings.port);
        server.run(settings.verbose);
    } catch (const std::string &error) {
        std::cout << "Got critical error " << error << ", aborting..." << std::endl;
    }
    

    return 0;
}