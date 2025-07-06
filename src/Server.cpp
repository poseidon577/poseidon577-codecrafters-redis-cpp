#include <iostream>
#include <string>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;
using namespace chrono;

// Global configuration map
unordered_map<string, string> server_config;

// Key-value store and expiry map
unordered_map<string, string> kv_store;
unordered_map<string, steady_clock::time_point> expiry_map;
mutex kv_mutex;

// RESP parser
vector<string> parse_resp(const string& input) {
    vector<string> result;
    size_t pos = 0;

    if (input[pos] != '*') return result;
    ++pos;

    size_t end = input.find("\r\n", pos);
    if (end == string::npos) return result;

    int array_len = stoi(input.substr(pos, end - pos));
    pos = end + 2;

    for (int i = 0; i < array_len; ++i) {
        if (pos >= input.size() || input[pos] != '$') return result;
        ++pos;

        end = input.find("\r\n", pos);
        if (end == string::npos) return result;

        int len = stoi(input.substr(pos, end - pos));
        pos = end + 2;

        if (pos + len > input.size()) return result;

        result.push_back(input.substr(pos, len));
        pos += len + 2;  // Skip value and trailing \r\n
    }

    return result;
}

void handle_client(int client_fd) {
    char buffer[1024];
    string full_msg;
    int bytes_received;

    cout << "Waiting for RESP input...\n";

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        full_msg += buffer;

        cout << "Received raw RESP:\n" << full_msg;

        vector<string> parts = parse_resp(full_msg);
        string response;

        if (!parts.empty()) {
            if (parts[0] == "PING") {
                response = "+PONG\r\n";

            } else if (parts[0] == "ECHO" && parts.size() == 2) {
                const string& msg = parts[1];
                response = "$" + to_string(msg.size()) + "\r\n" + msg + "\r\n";

            } else if (parts[0] == "SET") {
                if (parts.size() == 3) {
                    lock_guard<mutex> lock(kv_mutex);
                    kv_store[parts[1]] = parts[2];
                    expiry_map.erase(parts[1]);  // Clear expiry if set
                    response = "+OK\r\n";

                } else if (parts.size() == 5 && parts[3] == "px") {
                    lock_guard<mutex> lock(kv_mutex);
                    kv_store[parts[1]] = parts[2];
                    expiry_map[parts[1]] = steady_clock::now() + milliseconds(stoi(parts[4]));
                    response = "+OK\r\n";
                } else {
                    response = "-ERR wrong number of arguments for 'SET'\r\n";
                }

            } else if (parts[0] == "GET" && parts.size() == 2) {
                lock_guard<mutex> lock(kv_mutex);
                const string& key = parts[1];

                // Check expiry
                auto exp_it = expiry_map.find(key);
                if (exp_it != expiry_map.end() && steady_clock::now() > exp_it->second) {
                    kv_store.erase(key);
                    expiry_map.erase(key);
                }

                auto it = kv_store.find(key);
                if (it != kv_store.end()) {
                    const string& val = it->second;
                    response = "$" + to_string(val.size()) + "\r\n" + val + "\r\n";
                } else {
                    response = "$-1\r\n";  // Null bulk string
                }

            } else if (parts[0] == "CONFIG" && parts.size() == 3 && parts[1] == "GET") {
                const string& key = parts[2];
                if (server_config.count(key)) {
                    const string& val = server_config[key];
                    response = "*2\r\n$" + to_string(key.size()) + "\r\n" + key +
                               "\r\n$" + to_string(val.size()) + "\r\n" + val + "\r\n";
                } else {
                    response = "*0\r\n";
                }

            } else {
                response = "-ERR unknown command\r\n";
            }

            send(client_fd, response.c_str(), response.size(), 0);
        }

        full_msg.clear();  // Ready for next command
    }

    close(client_fd);
}

int main(int argc, char** argv) {
    cout << unitbuf;
    cerr << unitbuf;

    // Default config
    string config_dir = ".";
    string dbfilename = "dump.rdb";

    // Parse CLI args
    for (int i = 1; i < argc - 1; ++i) {
        string arg = argv[i];
        if (arg == "--dir") {
            config_dir = argv[i + 1];
        } else if (arg == "--dbfilename") {
            dbfilename = argv[i + 1];
        }
    }

    // Store config
    server_config["dir"] = config_dir;
    server_config["dbfilename"] = dbfilename;

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "Failed to create server socket\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    if (listen(server_fd, 5) != 0) {
        cerr << "listen failed\n";
        return 1;
    }

    // Accept client connections
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            cerr << "Failed to accept client\n";
            continue;
        }

        cout << "Accepted client: fd " << client_fd << "\n";
        thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}
