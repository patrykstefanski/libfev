#include <arpa/inet.h>
#include <netinet/in.h>

#include <charconv>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <system_error>
#include <utility>

#include <fev/fev++.hpp>

namespace {

struct sockaddr_in server_addr;

void echo(fev::socket &&socket)
try {
  char buffer[1024];
  for (;;) {
    std::size_t num_read = socket.read(buffer, sizeof(buffer));
    if (num_read == 0)
      break;

    socket.write(buffer, num_read);
  }
} catch (const std::system_error &e) {
  std::cerr << "[echo] " << e.what() << '\n';
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

    // If we don't specify any sched, the fiber will be spawned in the current scheduler.
    fev::fiber::spawn(&echo, std::move(new_socket));
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

  // Spawn a fiber in `sched`.
  fev::fiber::spawn(sched, &acceptor);

  sched.run();

  return 0;
}
