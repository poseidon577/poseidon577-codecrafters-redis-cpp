#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using std::string;
using std::vector;
using std::unordered_map;
using std::lock_guard;
using std::mutex;
using std::pair;
using std::cerr;
using std::cout;
using namespace std::chrono;

// Structure to store value and optional expiry time
struct Entry {
    string value;
    bool has_expiry = false;
    steady_clock::time_point expiry_time;
};

void handle_client(int client_fd) {
    char buffer[1024];
    string full_msg;
    int bytes_received;

    static unordered_map<string, Entry> kv_store;
    static mutex kv_mutex;

    cout << "Waiting for RESP input...\n";

    // Parse RESP message into vector of parts
    auto parse_resp = [](const string& input) -> vector<string> {
        vector<string> result;
        size_t pos = 0;

        if (pos >= input.size() || input[pos] != '*') return result;
        ++pos;

        size_t end = input.find("\r\n", pos);
        if (end == string::npos) return result;

        int array_len = std::stoi(input.substr(pos, end - pos));
        pos = end + 2;

        for (int i = 0; i < array_len; ++i) {
            if (pos >= input.size() || input[pos] != '$') return result;
            ++pos;

            end = input.find("\r\n", pos);
            if (end == string::npos) return result;

            int len = std::stoi(input.substr(pos, end - pos));
            pos = end + 2;

            if (pos + len > input.size()) return result;
            result.push_back(input.substr(pos, len));
            pos += len + 2;
        }

        return result;
    };

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
                const string& message = parts[1];
                response = "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
            } else if (parts[0] == "SET" && (parts.size() == 3 || parts.size() == 5)) {
                string key = parts[1];
                string value = parts[2];
                Entry entry{value};

                if (parts.size() == 5 && parts[3] == "PX") {
                    int ttl = std::stoi(parts[4]);
                    entry.has_expiry = true;
                    entry.expiry_time = steady_clock::now() + milliseconds(ttl);
                }

                {
                    lock_guard<mutex> lock(kv_mutex);
                    kv_store[key] = entry;
                }

                response = "+OK\r\n";
            } else if (parts[0] == "GET" && parts.size() == 2) {
                string key = parts[1];
                lock_guard<mutex> lock(kv_mutex);

                auto it = kv_store.find(key);
                if (it != kv_store.end()) {
                    Entry& entry = it->second;
                    if (entry.has_expiry && steady_clock::now() > entry.expiry_time) {
                        kv_store.erase(it);
                        response = "$-1\r\n";  // expired
                    } else {
                        const string& val = entry.value;
                        response = "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
                    }
                } else {
                    response = "$-1\r\n";  // key not found
                }
            } else {
                response = "-ERR unknown command\r\n";
            }

            send(client_fd, response.c_str(), response.size(), 0);
        }

        full_msg.clear();  // Ready for next command
    }

    close(client_fd); // Close socket on client disconnect
}

int main(int argc, char** argv) {
    cout << std::unitbuf;
    cerr << std::unitbuf;

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        cerr << "Failed to create server socket\n";
        return 1;
    }

    // Allow address reuse
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        cerr << "setsockopt failed\n";
        return 1;
    }

    // Bind socket to port 6379
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    // Start listening
    if (listen(server_fd, 5) != 0) {
        cerr << "listen failed\n";
        return 1;
    }

    // Accept clients in a loop
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            cerr << "Failed to accept client\n";
            continue;
        }

        cout << "Accepted client: fd " << client_fd << "\n";

        std::thread(handle_client, client_fd).detach(); // Spawn thread
    }

    close(server_fd); // Never reached
    return 0;
}
