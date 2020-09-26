# libfev

A library for events and fibers (a.k.a. green threads/goroutines/stackful coroutines).

[![CI](https://github.com/patrykstefanski/libfev/workflows/CI/badge.svg)](https://github.com/patrykstefanski/libfev/actions)

## Overview

libfev is an abstraction over event-driven, non-blocking I/O for writing programs in C and C++ in a
simple blocking style. It provides:

* Few multithreaded schedulers
* Backends for epoll and kqueue (and experimental io\_uring backend)
* Timers
* Synchronization primitives (mutex, condition variable and semaphore)

## Performance
In a throughput benchmark libfev can handle up to 172% more requests per second than
[Boost.Asio](https://www.boost.org/doc/libs/1_74_0/doc/html/boost_asio.html), up to 77% more than
[Tokio](https://tokio.rs/), up to 40% more than [async-std](https://async.rs/) and up to 16% more
than [Go](https://golang.org/). See [async-bench](https://github.com/patrykstefanski/async-bench)
for more data, there is also a comparison of the available schedulers.

## Support

Following platforms are currently supported:

* x86 (both 32- and 64-bit)
* FreeBSD, Linux, macOS
* DragonFlyBSD, NetBSD, OpenBSD should work too, but I haven't tested them yet
* Clang >= 9 or GCC >= 8

## Example

```cpp
void echo(fev::socket &&socket) try {
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

void acceptor() {
  fev::socket socket;
  socket.open(AF_INET, SOCK_STREAM, 0);
  socket.set_reuse_addr();
  socket.bind(reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr));
  socket.listen(1024);
  for (;;) {
    auto new_socket = socket.accept();
    fev::fiber::spawn(&echo, std::move(new_socket));
  }
}
```

See also [examples](examples).

## Documentation

* [Scheduler strategies](docs/SchedulerStrategies.md)

## License

Licensed under either of

 * Apache License, Version 2.0, ([LICENSE-APACHE](LICENSE-APACHE) or
   http://www.apache.org/licenses/LICENSE-2.0)
 * MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Third party

This library includes some code written by third parties. Check [third\_party](third_party) for
their licenses.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the
work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
