#include <arpa/inet.h>
#include <netinet/in.h>

#include <charconv>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <system_error>

#include <fev/fev++.hpp>

namespace {

struct sockaddr_in server_addr;

std::string_view message;

void client()
try {
  fev::socket socket;
  socket.open(AF_INET, SOCK_STREAM, 0);
  socket.connect(reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));

  // Send hello world message.
  socket.write(message.data(), message.length());

  // Receive response.
  char buffer[1024];
  std::size_t num_read = socket.read(buffer, sizeof(buffer));

  std::string_view response{buffer, num_read};
  std::cout << "Response: " << response << '\n';
} catch (const std::system_error &e) {
  std::cerr << "[client] " << e.what() << '\n';
}

} // namespace

int main(int argc, char **argv)
{
  // Parse arguments.

  if (argc != 4) {
    std::cerr << "Usage: " << argv[0] << " <HOST-IPV4> <PORT> <MESSAGE>\n";
    return 1;
  }

  auto host = argv[1];

  std::uint16_t port;
  if (auto [_, ec] = std::from_chars(argv[2], argv[2] + std::strlen(argv[2]), port);
      ec != std::errc{}) {
    std::cerr << "Failed to parse port\n";
    return 1;
  }

  message = argv[3];

  // Initialize server address.

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_aton(host, &server_addr.sin_addr) != 1) {
    std::cerr << "Converting host IPv4 '" << host << "' failed\n";
    return 1;
  }

  // Run.

  fev::sched sched{};

  // Spawn a fiber in `sched`.
  fev::fiber::spawn(sched, &client);

  sched.run();

  return 0;
}
