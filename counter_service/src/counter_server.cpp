#include <iostream>
#include <sstream>
#include <cstring>
#include <string>
#include <unordered_map>


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

volatile sig_atomic_t terminate_flag = 0;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        std::cout << "\nCtrl+C detected. Initiating graceful shutdown..." << std::endl;
        terminate_flag = 1; // Set the flag to request termination
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
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            throw std::string("setsockopt failed");
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

    void reply_client(int client_socket, const std::string &reply) {
        send(client_socket, reply.c_str(), reply.size(), 0);
    }

    void handle_client(int client_socket) {
        const int max_cmd_size = 1024;
        char buffer[max_cmd_size] = {0};
        recv(client_socket, buffer, max_cmd_size, 0);

        // we have command as pretty string now
        std::string command(buffer);
        
        std::cout << "got command " << command << std::endl;

        std::istringstream iss(command);
        std::string cmd_name; 
        iss >> cmd_name;
    
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
            throw std::string("got unknown command");
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

    void run(bool verbose) {
         // while (terminate_flag == 0) {
        while(true) {
            // waiting for clients connections
            sockaddr_in cli{}; socklen_t clilen = sizeof(cli);
            
            int client_socket = accept(server_fd, reinterpret_cast<sockaddr*>(&cli), &clilen);
            if (client_socket < 0) {
                std::string("Problem with listening to socket");
            }

            if (verbose) {
                char ip[INET_ADDRSTRLEN]{};
                ::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
                std::cerr << "Accepted connection from " << ip << ":" << ntohs(cli.sin_port) << "\n";
            }

            handle_client(client_socket);
            close(client_socket);
        }
    }
};

int main() {
    try {
        Settings settings = {.port = 9000, .verbose = true};

        Server server(settings.port);
        server.run(settings.verbose);
    } catch (const std::string &error) {
        std::cout << "Got critical error " << error << ", aborting..." << std::endl;
    }
    

    return 0;
}