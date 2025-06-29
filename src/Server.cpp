#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <algorithm> // for std::count

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

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";
  std::cout << "Logs from your program will appear here!\n";

  int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, &client_addr_len);
  std::cout << "Client connected\n";

  char buffer[1024];
  std::string full_msg;
  int bytes_received;
  std::cout << "Waiting for RESP input...\n";
  while ((bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
    buffer[bytes_received] = '\0';
    full_msg += buffer;

    // Count how many times "PING" appears
    size_t count = 0;
    size_t pos = 0;
    while ((pos = full_msg.find("PING", pos)) != std::string::npos) {
      ++count;
      pos += 4;
    }

    std::cout << "Received:\n" << full_msg;
    std::cout << "Found " << count << " PING command(s)\n";

    // Send back "PONG" that many times
    for (size_t i = 0; i < count; ++i) {
      std::string response = "+PONG\r\n";
      send(client_fd, response.c_str(), response.size(), 0);
    }

    full_msg.clear();  // reset to handle next round of input
  }

  close(client_fd);
  close(server_fd);
  return 0;
}
