#include <functional>
#include <iostream>

#include <fev/fev++.hpp>

// Note this library is not the right one for tasks like this. If you are looking for parallel
// tasks with dependencies, you should check taskflow library.

namespace {

constexpr int N = 20;

void fibonacci(int n, int &result)
{
  if (n <= 1) {
    result = n;
    return;
  }

  int result1, result2;
  fev::fiber fiber1{&fibonacci, n - 1, std::ref(result1)};
  fev::fiber fiber2{&fibonacci, n - 2, std::ref(result2)};
  fiber1.join();
  fiber2.join();
  result = result1 + result2;
}

} // namespace

int main()
{
  fev::sched sched{};

  int result;
  fev::fiber::spawn(sched, &fibonacci, N, std::ref(result));

  sched.run();

  std::cout << "fibonacci(" << N << ") = " << result << '\n';
}
