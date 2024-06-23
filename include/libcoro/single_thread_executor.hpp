#ifndef SINGLE_THREAD_EXECUTOR_HPP
#define SINGLE_THREAD_EXECUTOR_HPP

#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <thread>

namespace libcoro {
class SingleThreadExecutor {
public:
  SingleThreadExecutor();
  ~SingleThreadExecutor();

  SingleThreadExecutor(const SingleThreadExecutor&) = delete;
  SingleThreadExecutor& operator=(const SingleThreadExecutor&) = delete;
  SingleThreadExecutor(SingleThreadExecutor&&) = delete;
  SingleThreadExecutor& operator=(SingleThreadExecutor&&) = delete;

  class Awaiter {
    friend class SingleThreadExecutor;
    explicit Awaiter(SingleThreadExecutor& executor) noexcept: _executor(executor) {}

  public:
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
      _handle = handle;
      _executor.execute(_handle);
    }
    void await_resume() noexcept {}

  private:
    SingleThreadExecutor& _executor;
    std::coroutine_handle<> _handle{nullptr};
  };

  void shutdown();

  Awaiter start() { return Awaiter{*this}; }
  void resume(std::coroutine_handle<>);

private:
  void execute(std::coroutine_handle<> handle);
  void background_thread();

  std::coroutine_handle<> _handle{nullptr};
  std::atomic<bool> _shutdown_requested{false};

  std::thread _execute_thread;
  std::mutex _wait_mutex{};
  std::condition_variable _wait_cv{};
};
} // namespace libcoro

#endif // !SINGLE_THREAD_EXECUTOR_HPP
