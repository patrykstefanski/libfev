/*
 * Copyright 2020 Patryk Stefanski
 *
 * Licensed under the Apache License, Version 2.0, <LICENSE-APACHE or
 * http://apache.org/licenses/LICENSE-2.0> or the MIT license <LICENSE-MIT or
 * http://opensource.org/licenses/MIT>, at your option. This file may not be
 * copied, modified, or distributed except according to those terms.
 */

#ifndef FEV_FEVPP_HPP
#define FEV_FEVPP_HPP

#include <fev/fev.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>

namespace fev {

namespace detail {

template <typename T> std::decay_t<T> decay_copy(T &&v) { return std::forward<T>(v); }

[[noreturn]] void throw_err(int err, const char *what)
{
  throw std::system_error{-err, std::generic_category(), what};
}

void throw_on_err(int err, const char *what)
{
  if (err != 0)
    throw_err(err, what);
}

template <typename Rep, typename Period>
timespec duration_to_timespec(const std::chrono::duration<Rep, Period> &rel_time)
{
  using Sec = std::chrono::seconds::rep;
  auto chrono_sec = std::chrono::duration_cast<std::chrono::seconds>(rel_time).count();
  if constexpr (std::numeric_limits<Sec>::max() > std::numeric_limits<time_t>::max() ||
                std::numeric_limits<Sec>::min() < std::numeric_limits<time_t>::min()) {
    if (chrono_sec > std::numeric_limits<time_t>::max() ||
        chrono_sec < std::numeric_limits<time_t>::min()) {
      throw_err(-EOVERFLOW, "Converting duration failed");
    }
  }
  auto sec = static_cast<time_t>(chrono_sec);

  static_assert(std::numeric_limits<long>::max() >= 999'999'999);
  auto nsec = static_cast<long>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(rel_time).count() % 1'000'000'000);

  return {sec, nsec};
}

} // namespace detail

class sched_attr final {
private:
  static fev_sched_attr *create()
  {
    fev_sched_attr *sched_attr;
    int err = fev_sched_attr_create(&sched_attr);
    detail::throw_on_err(err, "Creating scheduler attributes failed");
    return sched_attr;
  }

public:
  sched_attr() : impl_{create(), &fev_sched_attr_destroy} {}

  sched_attr(const sched_attr &) = delete;
  void operator=(const sched_attr &) = delete;

  sched_attr(sched_attr &&) = default;
  sched_attr &operator=(sched_attr &&) = default;

  std::uint32_t num_workers() const noexcept { return fev_sched_attr_get_num_workers(impl()); }

  void set_num_workers(std::uint32_t num_workers) noexcept
  {
    fev_sched_attr_set_num_workers(impl(), num_workers);
  }

  const fev_sched_attr *impl() const noexcept { return impl_.get(); }
  fev_sched_attr *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_sched_attr, void (*)(fev_sched_attr *)> impl_;
};

class sched final {
private:
  static fev_sched *create(const fev_sched_attr *attr)
  {
    fev_sched *sched;
    int err = fev_sched_create(&sched, attr);
    detail::throw_on_err(err, "Creating scheduler failed");
    return sched;
  }

  explicit sched(const fev_sched_attr *attr) : impl_{create(attr), &fev_sched_destroy} {}

public:
  sched() : sched{nullptr} {}
  explicit sched(const sched_attr &attr) : sched{attr.impl()} {}

  sched(const sched &) = delete;
  void operator=(const sched &) = delete;

  sched(sched &&) = default;
  sched &operator=(sched &&) = default;

  void run()
  {
    int err = fev_sched_run(impl());
    detail::throw_on_err(err, "Running scheduler failed");
  }

  const fev_sched *impl() const noexcept { return impl_.get(); }
  fev_sched *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_sched, void (*)(fev_sched *)> impl_;
};

class fiber_attr final {
private:
  static fev_fiber_attr *create()
  {
    fev_fiber_attr *fiber_attr;
    int err = fev_fiber_attr_create(&fiber_attr);
    detail::throw_on_err(err, "Creating fiber attributes failed");
    return fiber_attr;
  }

public:
  fiber_attr() : impl_{create(), &fev_fiber_attr_destroy} {}

  fiber_attr(const fiber_attr &) = delete;
  void operator=(const fiber_attr &) = delete;

  fiber_attr(fiber_attr &&) = default;
  fiber_attr &operator=(fiber_attr &&) = default;

  std::pair<void *, std::size_t> stack() noexcept
  {
    void *stack_addr;
    std::size_t stack_size;
    fev_fiber_attr_get_stack(impl(), &stack_addr, &stack_size);
    return {stack_addr, stack_size};
  }

  void set_stack(void *stack_addr, std::size_t stack_size)
  {
    int err = fev_fiber_attr_set_stack(impl(), stack_addr, stack_size);
    detail::throw_on_err(err, "Setting stack failed");
  }

  std::size_t stack_size() const noexcept { return fev_fiber_attr_get_stack_size(impl()); }

  void set_stack_size(std::size_t stack_size)
  {
    int err = fev_fiber_attr_set_stack_size(impl(), stack_size);
    detail::throw_on_err(err, "Setting stack size failed");
  }

  std::size_t guard_size() const noexcept { return fev_fiber_attr_get_guard_size(impl()); }

  void set_guard_size(std::size_t guard_size)
  {
    int err = fev_fiber_attr_set_guard_size(impl(), guard_size);
    detail::throw_on_err(err, "Setting guard size failed");
  }

  bool detached() const noexcept { return fev_fiber_attr_get_detached(impl()); }

  void set_detached(bool detached) noexcept { fev_fiber_attr_set_detached(impl(), detached); }

  const fev_fiber_attr *impl() const noexcept { return impl_.get(); }
  fev_fiber_attr *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_fiber_attr, void (*)(fev_fiber_attr *)> impl_;
};

class fiber final {
private:
  template <typename FuncArgs, std::size_t... Indices>
  static void start(FuncArgs &func_args, std::index_sequence<Indices...>)
  {
    std::invoke(std::move(std::get<Indices>(func_args))...);
  }

  template <typename FuncArgs> static void *proxy(void *arg)
  {
    std::unique_ptr<FuncArgs> func_args{static_cast<FuncArgs *>(arg)};
    try {
      start(*func_args, std::make_index_sequence<std::tuple_size_v<FuncArgs>>{});
    } catch (...) {
      std::cerr << "Uncaught exception in fiber\n";
      std::terminate();
    }
    return nullptr;
  }

  template <typename Func, typename... Args>
  void create_impl(fev_sched *sched, const fev_fiber_attr *fiber_attr, Func &&func, Args &&... args)
  {
    fev_fiber *fiber;
    using FuncArgs = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
    std::unique_ptr<FuncArgs> func_args{
        new FuncArgs{detail::decay_copy(std::forward<Func>(func)),
                     detail::decay_copy(std::forward<Args>(args))...}};
    int err = fev_fiber_create(&fiber, sched, &proxy<FuncArgs>, func_args.get(), fiber_attr);
    detail::throw_on_err(err, "Creating fiber failed");
    func_args.release();

    // Store fiber pointer if the fiber is joinable (not detached).
    // This assumes that a fiber created with default attributes is joinable (this is set in
    // fev_fiber_attr.c).
    if (fiber_attr == nullptr || !fev_fiber_attr_get_detached(fiber_attr))
      impl_ = fiber;
  }

  template <typename Func, typename... Args>
  static void spawn_impl(fev_sched *sched, Func &&func, Args &&... args)
  {
    using FuncArgs = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
    std::unique_ptr<FuncArgs> func_args{
        new FuncArgs{detail::decay_copy(std::forward<Func>(func)),
                     detail::decay_copy(std::forward<Args>(args))...}};
    int err = fev_fiber_spawn(sched, &proxy<FuncArgs>, func_args.get());
    detail::throw_on_err(err, "Spawning fiber failed");
    func_args.release();
  }

public:
  fiber() noexcept = default;

  fiber(const fiber &) = delete;
  void operator=(const fiber &) = delete;

  fiber(fiber &&other) noexcept : impl_{other.impl_} { other.impl_ = nullptr; }

  fiber &operator=(fiber &&other) noexcept
  {
    if (joinable()) {
      std::cerr << "Assignment operator called on joinable fiber\n";
      std::terminate();
    }

    impl_ = other.impl_;
    other.impl_ = nullptr;
    return *this;
  }

  template <typename Func, typename... Args> fiber(Func &&func, Args &&... args)
  {
    create_impl(nullptr, nullptr, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args> fiber(sched &sched, Func &&func, Args &&... args)
  {
    create_impl(sched.impl(), nullptr, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  fiber(const fiber_attr &fiber_attr, Func &&func, Args &&... args)
  {
    create_impl(nullptr, fiber_attr.impl(), std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  fiber(sched &sched, const fiber_attr &fiber_attr, Func &&func, Args &&... args)
  {
    create_impl(sched.impl(), fiber_attr.impl(), std::forward<Func>(func),
                std::forward<Args>(args)...);
  }

  ~fiber()
  {
    if (joinable()) {
      std::cerr << "Destructor called on joinable fiber\n";
      std::terminate();
    }
  }

  template <typename Func, typename... Args> static void spawn(Func &&func, Args &&... args)
  {
    spawn_impl(nullptr, std::forward<Func>(func), std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  static void spawn(sched &sched, Func &&func, Args &&... args)
  {
    spawn_impl(sched.impl(), std::forward<Func>(func), std::forward<Args>(args)...);
  }

  bool joinable() const noexcept { return impl_ != nullptr; }

  void join()
  {
    int err = -EINVAL;
    if (joinable())
      err = fev_fiber_join(impl_, /*return_value_ptr=*/nullptr);
    detail::throw_on_err(err, "Joining fiber failed");
    impl_ = nullptr;
  }

  void detach()
  {
    int err = -EINVAL;
    if (joinable())
      err = fev_fiber_detach(impl_);
    detail::throw_on_err(err, "Detaching fiber failed");
    impl_ = nullptr;
  }

  void swap(fiber &other) noexcept { std::swap(impl_, other.impl_); }

  const fev_fiber *impl() const noexcept { return impl_; }
  fev_fiber *impl() noexcept { return impl_; }

private:
  fev_fiber *impl_{nullptr};
};

namespace this_fiber {

// TODO: Add sleep_for() and sleep_until() on top of fev_sleep_for() and fev_sleep_until(), when
// they are implemented.

inline void yield() noexcept { fev_yield(); }

} // namespace this_fiber

class mutex final {
private:
  fev_mutex *create()
  {
    fev_mutex *mutex;
    int err = fev_mutex_create(&mutex);
    detail::throw_on_err(err, "Creating mutex failed");
    return mutex;
  }

public:
  mutex() : impl_{create(), &fev_mutex_destroy} {}

  mutex(const mutex &) = delete;
  void operator=(const mutex &) = delete;

  mutex(mutex &&) = default;
  mutex &operator=(mutex &&) = default;

  void lock() noexcept { fev_mutex_lock(impl()); }

  bool try_lock() noexcept { return fev_mutex_try_lock(impl()); }

  bool try_lock_until(const timespec &abs_time)
  {
    int ret = fev_mutex_try_lock_until(impl(), &abs_time);
    if (ret == 0)
      return true;
    if (ret == -ETIMEDOUT)
      return false;
    detail::throw_err(ret, "Trying to lock mutex failed");
  }

  bool try_lock_for(const timespec &rel_time)
  {
    int ret = fev_mutex_try_lock_for(impl(), &rel_time);
    if (ret == 0)
      return true;
    if (ret == -ETIMEDOUT)
      return false;
    detail::throw_err(ret, "Trying to lock mutex failed");
  }

  template <typename Rep, typename Period>
  bool try_lock_for(const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return try_lock_for(ts);
  }

  void unlock() noexcept { fev_mutex_unlock(impl()); }

  const fev_mutex *impl() const noexcept { return impl_.get(); }
  fev_mutex *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_mutex, void (*)(fev_mutex *)> impl_;
};

class condition_variable final {
private:
  fev_cond *create()
  {
    fev_cond *cond;
    int err = fev_cond_create(&cond);
    detail::throw_on_err(err, "Creating condition variable failed");
    return cond;
  }

public:
  condition_variable() : impl_{create(), &fev_cond_destroy} {}

  condition_variable(const condition_variable &) = delete;
  void operator=(const condition_variable &) = delete;

  condition_variable(condition_variable &&) = default;
  condition_variable &operator=(condition_variable &&) = default;

  void notify_one() noexcept { fev_cond_notify_one(impl()); }

  void notify_all() noexcept { fev_cond_notify_all(impl()); }

  void wait(std::unique_lock<fev::mutex> &lock) noexcept
  {
    fev_cond_wait(impl(), lock.mutex()->impl());
  }

  template <typename Predicate>
  void wait(std::unique_lock<fev::mutex> &lock, Predicate pred) noexcept
  {
    while (!pred())
      wait(lock);
  }

  std::cv_status wait_until(std::unique_lock<fev::mutex> &lock, const timespec &abs_time)
  {
    int ret = fev_cond_wait_until(impl(), lock.mutex()->impl(), &abs_time);
    if (ret == 0 || ret == -EAGAIN)
      return std::cv_status::no_timeout;
    if (ret == -ETIMEDOUT)
      return std::cv_status::timeout;
    detail::throw_err(ret, "Waiting on condition variable failed");
  }

  template <typename Predicate>
  bool wait_until(std::unique_lock<fev::mutex> &lock, const timespec &abs_time, Predicate pred)
  {
    while (!pred()) {
      auto status = wait_until(lock, abs_time);
      if (status == std::cv_status::timeout)
        return pred();
    }
    return true;
  }

  std::cv_status wait_for(std::unique_lock<fev::mutex> &lock, const timespec &rel_time)
  {
    int ret = fev_cond_wait_for(impl(), lock.mutex()->impl(), &rel_time);
    if (ret == 0 || ret == -EAGAIN)
      return std::cv_status::no_timeout;
    if (ret == -ETIMEDOUT)
      return std::cv_status::timeout;
    detail::throw_err(ret, "Waiting on condition variable failed");
  }

  template <typename Predicate>
  bool wait_for(std::unique_lock<fev::mutex> &lock, const timespec &rel_time, Predicate pred)
  {
    // TODO: Allow user to choose clock id.
    clockid_t clock_id = CLOCK_MONOTONIC;
    timespec abs_time;
    int err = clock_gettime(clock_id, &abs_time);
    if (err < 0)
      detail::throw_err(-errno, "Getting time failed");
    abs_time.tv_sec += rel_time.tv_sec + (abs_time.tv_nsec + rel_time.tv_nsec) / 1'000'000'000;
    abs_time.tv_nsec = (abs_time.tv_nsec + rel_time.tv_nsec) % 1'000'000'000;
    return wait_until(lock, abs_time, std::move(pred));
  }

  template <typename Rep, typename Period>
  std::cv_status wait_for(std::unique_lock<fev::mutex> &lock,
                          const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return wait_for(lock, ts);
  }

  template <typename Rep, typename Period, typename Predicate>
  bool wait_for(std::unique_lock<fev::mutex> &lock,
                const std::chrono::duration<Rep, Period> &rel_time, Predicate pred)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return wait_for(lock, ts, std::move(pred));
  }

  const fev_cond *impl() const noexcept { return impl_.get(); }
  fev_cond *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_cond, void (*)(fev_cond *)> impl_;
};

class semaphore final {
private:
  static fev_sem *create(std::int32_t value)
  {
    fev_sem *sem;
    int err = fev_sem_create(&sem, value);
    detail::throw_on_err(err, "Creating semaphore failed");
    return sem;
  }

public:
  explicit semaphore(std::int32_t value) : impl_{create(value), &fev_sem_destroy} {}

  semaphore(const semaphore &) = delete;
  void operator=(const semaphore &) = delete;

  semaphore(semaphore &&) = default;
  semaphore &operator=(semaphore &&) = default;

  void post() noexcept { fev_sem_post(impl()); }

  void wait() noexcept { fev_sem_wait(impl()); }

  bool wait_until(const timespec &abs_time)
  {
    int ret = fev_sem_wait_until(impl(), &abs_time);
    if (ret == 0)
      return true;
    if (ret == -ETIMEDOUT)
      return false;
    detail::throw_err(ret, "Waiting on semaphore failed");
  }

  bool wait_for(const timespec &rel_time)
  {
    int ret = fev_sem_wait_for(impl(), &rel_time);
    if (ret == 0)
      return true;
    if (ret == -ETIMEDOUT)
      return false;
    detail::throw_err(ret, "Waiting on semaphore failed");
  }

  template <typename Rep, typename Period>
  bool wait_for(const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return wait_for(ts);
  }

  const fev_sem *impl() const noexcept { return impl_.get(); }
  fev_sem *impl() noexcept { return impl_.get(); }

private:
  std::unique_ptr<fev_sem, void (*)(fev_sem *)> impl_;
};

class socket final {
private:
  static fev_socket *create()
  {
    fev_socket *socket;
    int err = fev_socket_create(&socket);
    detail::throw_on_err(err, "Creating socket failed");
    return socket;
  }

public:
  socket() : impl_{create(), &fev_socket_destroy} {}

  socket(const socket &) = delete;
  void operator=(const socket &) = delete;

  socket(socket &&) = default;
  socket &operator=(socket &&) = default;

  ~socket()
  {
    if (impl() != nullptr) {
      try {
        close();
      } catch (...) {
        std::cerr << "Closing socket in destructor failed\n";
        std::terminate();
      }
    }
  }

  int native_handle() const noexcept { return fev_socket_native_handle(impl()); }

  void set_opt(int level, int option_name, const void *option_value, socklen_t option_len)
  {
    int err = fev_socket_set_opt(impl(), level, option_name, option_value, option_len);
    detail::throw_on_err(err, "Setting socket option failed");
  }

  void set_reuse_addr()
  {
    int val{1};
    set_opt(SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  }

  void open(int domain, int type, int protocol)
  {
    int err = fev_socket_open(impl(), domain, type, protocol);
    detail::throw_on_err(err, "Opening socket failed");
  }

  void close()
  {
    int err = fev_socket_close(impl());
    detail::throw_on_err(err, "Closing socket failed");
  }

  void bind(sockaddr *address, socklen_t address_len)
  {
    int err = fev_socket_bind(impl(), address, address_len);
    detail::throw_on_err(err, "Binding socket failed");
  }

  void listen(int backlog)
  {
    int err = fev_socket_listen(impl(), backlog);
    detail::throw_on_err(err, "Listening on socket failed");
  }

  const fev_socket *impl() const noexcept { return impl_.get(); }
  fev_socket *impl() noexcept { return impl_.get(); }

  // Accept

  socket accept(sockaddr *address, socklen_t *address_len)
  {
    socket new_socket{};
    int err = fev_socket_accept(impl(), new_socket.impl(), address, address_len);
    detail::throw_on_err(err, "Accepting socket failed");
    return new_socket;
  }

  socket accept() { return accept(nullptr, nullptr); }

  socket try_accept_until(sockaddr *address, socklen_t *address_len, const timespec &abs_time)
  {
    socket new_socket{};
    int err =
        fev_socket_try_accept_until(impl(), new_socket.impl(), address, address_len, &abs_time);
    detail::throw_on_err(err, "Accepting socket failed");
    return new_socket;
  }

  socket try_accept_until(const timespec &abs_time)
  {
    return try_accept_until(nullptr, nullptr, abs_time);
  }

  socket try_accept_for(sockaddr *address, socklen_t *address_len, const timespec &rel_time)
  {
    socket new_socket{};
    int err = fev_socket_try_accept_for(impl(), new_socket.impl(), address, address_len, &rel_time);
    detail::throw_on_err(err, "Accepting socket failed");
    return new_socket;
  }

  socket try_accept_for(const timespec &rel_time)
  {
    return try_accept_for(nullptr, nullptr, rel_time);
  }

  template <typename Rep, typename Period>
  socket try_accept_for(sockaddr *address, socklen_t *address_len,
                        const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return try_accept_for(address, address_len, ts);
  }

  template <typename Rep, typename Period>
  socket try_accept_for(const std::chrono::duration<Rep, Period> &rel_time)
  {
    return try_accept_for(nullptr, nullptr, rel_time);
  }

  // Connect

  void connect(sockaddr *address, socklen_t address_len)
  {
    int err = fev_socket_connect(impl(), address, address_len);
    detail::throw_on_err(err, "Connecting failed");
  }

  void try_connect_until(sockaddr *address, socklen_t address_len, const timespec &abs_time)
  {
    int err = fev_socket_try_connect_until(impl(), address, address_len, &abs_time);
    detail::throw_on_err(err, "Connecting failed");
  }

  void try_connect_for(sockaddr *address, socklen_t address_len, const timespec &rel_time)
  {
    int err = fev_socket_try_connect_for(impl(), address, address_len, &rel_time);
    detail::throw_on_err(err, "Connecting failed");
  }

  template <typename Rep, typename Period>
  void try_connect_for(sockaddr *address, socklen_t address_len,
                       const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    try_connect_for(address, address_len, ts);
  }

  // Read

  std::size_t read(void *buffer, std::size_t size)
  {
    ssize_t ret = fev_socket_read(impl(), buffer, size);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Reading from socket failed");
    return static_cast<std::size_t>(ret);
  }

  std::size_t try_read_until(void *buffer, std::size_t size, const timespec &abs_time)
  {
    ssize_t ret = fev_socket_try_read_until(impl(), buffer, size, &abs_time);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Reading from socket failed");
    return static_cast<std::size_t>(ret);
  }

  std::size_t try_read_for(void *buffer, std::size_t size, const timespec &rel_time)
  {
    ssize_t ret = fev_socket_try_read_for(impl(), buffer, size, &rel_time);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Reading from socket failed");
    return static_cast<std::size_t>(ret);
  }

  template <typename Rep, typename Period>
  std::size_t try_read_for(void *buffer, std::size_t size,
                           const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return try_read_for(buffer, size, ts);
  }

  // Write

  std::size_t write(const void *buffer, std::size_t size)
  {
    ssize_t ret = fev_socket_write(impl(), buffer, size);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Writing to socket failed");
    return static_cast<std::size_t>(ret);
  }

  std::size_t try_write_until(const void *buffer, std::size_t size, const timespec &abs_time)
  {
    ssize_t ret = fev_socket_try_write_until(impl(), buffer, size, &abs_time);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Writing to socket failed");
    return static_cast<std::size_t>(ret);
  }

  std::size_t try_write_for(const void *buffer, std::size_t size, const timespec &rel_time)
  {
    ssize_t ret = fev_socket_try_write_for(impl(), buffer, size, &rel_time);
    if (ret < 0)
      detail::throw_err(static_cast<int>(ret), "Writing to socket failed");
    return static_cast<std::size_t>(ret);
  }

  template <typename Rep, typename Period>
  std::size_t try_write_for(const void *buffer, std::size_t size,
                            const std::chrono::duration<Rep, Period> &rel_time)
  {
    const auto ts = detail::duration_to_timespec(rel_time);
    return try_write_for(buffer, size, ts);
  }

private:
  std::unique_ptr<fev_socket, void (*)(fev_socket *)> impl_;
};

} // namespace fev

namespace std {

inline void swap(fev::fiber &a, fev::fiber &b) noexcept { a.swap(b); }

} // namespace std

#endif // !FEV_FEVPP_HPP
