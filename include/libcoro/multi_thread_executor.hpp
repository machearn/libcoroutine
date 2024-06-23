#ifndef MULTI_THREAD_EXECOTOR_HPP
#define MULTI_THREAD_EXECOTOR_HPP

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>

namespace libcoro {
class MultiThreadExecutor {
public:
  explicit MultiThreadExecutor(std::size_t size);
  ~MultiThreadExecutor();

  MultiThreadExecutor(const MultiThreadExecutor&) = delete;
  MultiThreadExecutor& operator=(const MultiThreadExecutor&) = delete;
  MultiThreadExecutor(MultiThreadExecutor&&) = delete;
  MultiThreadExecutor& operator=(MultiThreadExecutor&&) = delete;

  class Awaiter {
    friend class MultiThreadExecutor;

    explicit Awaiter(MultiThreadExecutor& executor) noexcept: _executor(executor) {}

  public:
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
      _handle = handle;
      _executor.execute(_handle);
    }
    void await_resume() noexcept {}

  private:
    MultiThreadExecutor& _executor;
    std::coroutine_handle<> _handle{nullptr};
  };

  Awaiter start();
  void resume(std::coroutine_handle<> handle);
  void shutdown();

private:
  void execute(std::coroutine_handle<> handle);
  void thread_function(std::size_t idx);

  std::vector<std::thread> _threads{};

  std::atomic<std::size_t> _size{0};
  std::deque<std::coroutine_handle<>> _handles{};

  std::mutex _wait_mutex{};
  std::condition_variable _wait_cv{};

  std::atomic<bool> _shutdown_requested{false};
};
} // namespace libcoro

#endif // !MULTI_THREAD_EXECOTOR_HPP
