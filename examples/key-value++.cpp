#include <arpa/inet.h>
#include <netinet/in.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <fev/fev++.hpp>

namespace {

struct sockaddr_in server_addr;

// A mutex between fibers.
fev::mutex mutex{};

std::unordered_map<std::string, std::string> data{};

void client(fev::socket &&socket)
try {
  char buffer[1024];
  for (;;) {
    std::size_t num_read = socket.read(buffer, sizeof(buffer));
    if (num_read == 0) {
      // EOF
      break;
    }

    std::string_view msg{buffer, num_read};

    // Trim
    while (msg.size() > 0 && std::isspace(msg[0]))
      msg.remove_prefix(1);
    while (msg.size() > 0 && std::isspace(msg[msg.size() - 1]))
      msg.remove_suffix(1);

    std::string_view cmd{}, key{};
    if (auto cmd_end = msg.find(' '); cmd_end != std::string_view::npos) {
      cmd = msg.substr(0, cmd_end);
      key = msg.substr(cmd_end + 1);
    }

    if (key.empty()) {
      std::string_view resp{"Parsing failed\n"};
      socket.write(resp.data(), resp.size());
      continue;
    }

    if (cmd == "get") {
      auto lock = std::unique_lock{mutex};
      auto search = data.find(std::string{key});
      if (search != data.end()) {
        auto value = search->second;
        lock.unlock();
        value += '\n';
        socket.write(value.data(), value.size());
      } else {
        lock.unlock();
        std::string_view resp{"Not found\n"};
        socket.write(resp.data(), resp.size());
      }
    } else if (cmd == "set") {
      std::string k{}, value{};
      if (auto key_end = key.find(' '); key_end != std::string_view::npos) {
        k = key.substr(0, key_end);
        value = key.substr(key_end + 1);
        auto lock = std::unique_lock{mutex};
        data[k] = value;
        lock.unlock();
        std::string_view resp{"OK\n"};
        socket.write(resp.data(), resp.size());
      } else {
        std::string_view resp{"Parsing failed\n"};
        socket.write(resp.data(), resp.size());
      }
    } else {
      std::string_view resp{"Unknown command\n"};
      socket.write(resp.data(), resp.size());
    }
  }
} catch (const std::system_error &e) {
  std::cerr << "[client] " << e.what() << '\n';
}

void acceptor()
{
  fev::socket socket;
  socket.open(AF_INET, SOCK_STREAM, 0);
  socket.set_reuse_addr();
  socket.bind(reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
  socket.listen(1024);
  for (;;) {
    auto new_socket = socket.accept();
    fev::fiber::spawn(&client, std::move(new_socket));
  }
}

} // namespace

int main(int argc, char **argv)
{
  // Parse arguments.

  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <HOST-IPV4> <PORT>\n";
    return 1;
  }

  auto host = argv[1];

  std::uint16_t port;
  if (auto [_, ec] = std::from_chars(argv[2], argv[2] + std::strlen(argv[2]), port);
      ec != std::errc{}) {
    std::cerr << "Failed to parse port\n";
    return 1;
  }

  // Initialize server address.

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    std::cerr << "Converting host IPv4 '" << host << "' failed\n";
    return 1;
  }

  // Run.

  fev::sched sched{};
  fev::fiber::spawn(sched, &acceptor);
  sched.run();

  return 0;
}
