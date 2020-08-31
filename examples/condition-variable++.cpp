#include <iostream>
#include <mutex>
#include <string>

#include <fev/fev++.hpp>

// Based on:
// https://en.cppreference.com/w/cpp/thread/condition_variable

namespace {

fev::mutex m{};
fev::condition_variable cv{};
std::string data{};
bool ready = false;
bool processed = false;

void worker()
{
  std::unique_lock<fev::mutex> lock{m};
  cv.wait(lock, [] { return ready; });

  std::cout << "worker is processing data\n";
  data += " after processing";

  processed = true;
  std::cout << "worker signals data processing completed\n";

  lock.unlock();
  cv.notify_one();
}

void manager()
{
  data = "example";

  {
    std::lock_guard<fev::mutex> lock{m};
    ready = true;
    std::cout << "manager signals data ready for processing\n";
  }
  cv.notify_one();

  {
    std::unique_lock<fev::mutex> lock{m};
    cv.wait(lock, [] { return processed; });
  }
  std::cout << "manager received processed data: " << data << "\n";
}

} // namespace

int main()
{
  fev::sched sched{};
  fev::fiber::spawn(sched, &manager);
  fev::fiber::spawn(sched, &worker);
  sched.run();
}
