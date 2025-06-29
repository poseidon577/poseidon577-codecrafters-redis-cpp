#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <algorithm>
#include <thread>
#include <bits/stdc++.h>

void handle_client(int client_fd)
{
    char buffer[1024];
    std::string full_msg;
    int bytes_received;

    std::cout << "Waiting for RESP input...\n";

    auto parse_resp = [](const std::string& input) -> std::vector<std::string> {
        std::vector<std::string> result;
        size_t pos = 0;

        if (pos >= input.size() || input[pos] != '*') return result;
        ++pos;

        size_t end = input.find("\r\n", pos);
        if (end == std::string::npos) return result;

        int array_len = std::stoi(input.substr(pos, end - pos));
        pos = end + 2;

        for (int i = 0; i < array_len; ++i) {
            if (pos >= input.size() || input[pos] != '$') return result;
            ++pos;

            end = input.find("\r\n", pos);
            if (end == std::string::npos) return result;

            int len = std::stoi(input.substr(pos, end - pos));
            pos = end + 2;

            if (pos + len > input.size()) return result;
            std::string value = input.substr(pos, len);
            result.push_back(value);
            pos += len + 2; // Skip value and following \r\n
        }

        return result;
    };

    while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        full_msg += buffer;

        std::cout << "Received raw RESP:\n" << full_msg;

        std::vector<std::string> parts = parse_resp(full_msg);

        if (!parts.empty()) {
            if (parts[0] == "ECHO" && parts.size() == 2) {
                const std::string& message = parts[1];
                std::string response = "$" + std::to_string(message.size()) + "\r\n" + message + "\r\n";
                send(client_fd, response.c_str(), response.size(), 0);
            } else {
                std::string error = "-ERR unknown command\r\n";
                send(client_fd, error.c_str(), error.size(), 0);
            }
        }

        full_msg.clear();  // Reset to allow new command in next recv()
    }

    close(client_fd);
}

int main(int argc, char **argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);

  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  if (listen(server_fd, 5) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

 while (true) 
 {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
      if (client_fd < 0) {
          std::cerr << "Failed to accept client\n";
          continue;
      }

      std::cout << "Accepted client: fd " << client_fd << "\n";

      // Spawn a new thread for each client
      std::thread(handle_client, client_fd).detach();
  }

  
  close(server_fd);
  return 0;
}
