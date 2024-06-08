#ifndef IO_SERVICE_HPP
#define IO_SERVICE_HPP

#include "libcoro/event_fd.hpp"
#include "libcoro/task.hpp"
#include <atomic>
#include <coroutine>
#include <mutex>
#include <sys/event.h>
#include <thread>

namespace libcoro {
class IOService {
public:
  IOService();
  ~IOService();

  IOService(const IOService&) = delete;
  IOService& operator=(const IOService&) = delete;
  IOService(IOService&&) = delete;
  IOService& operator=(IOService&&) = delete;

  class ExecuteAwaiter {
    friend class IOService;
    explicit ExecuteAwaiter(IOService& io_service) noexcept: _io_service(io_service) {}

  public:
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept {
      _io_service._awaiting_size.fetch_add(1, std::memory_order_release);
      {
        std::scoped_lock lock(_io_service._awaiting_coroutines_mutex);
        _io_service._awaiting_coroutines.push_back(handle);
      }

      bool expected = false;
      if (_io_service._scheduler_event_fd_triggered.compare_exchange_strong(
              expected, true, std::memory_order_release, std::memory_order_relaxed)) {
#ifdef __APPLE__
        _io_service._scheduler_event_fd.trigger();
#elif __linux__
#endif
      }
    }
    void await_resume() noexcept {}

  private:
    IOService& _io_service;
  };

  ExecuteAwaiter await() { return ExecuteAwaiter{*this}; }
  void execute(Task<void>&& task);

  std::size_t size() const noexcept { return _awaiting_size.load(std::memory_order_acquire); }

private:
  void process_tasks_background();
  void process_scheduled_tasks();

  std::thread _io_thread;

  int _poll_fd{-1};

#ifdef __APPLE__
  detail::EventFD _scheduler_event_fd{};
  std::array<struct kevent, 16> _events{};
#elif __linux__
  // clang-format off
  int _scheduler_event_fd;
  // clang-format on
#endif
  std::atomic<bool> _scheduler_event_fd_triggered{false};
  std::mutex _awaiting_coroutines_mutex{};
  std::vector<std::coroutine_handle<>> _awaiting_coroutines{};

  std::atomic<std::size_t> _awaiting_size{0};

  std::atomic<bool> _close_requested{false};
};
} // namespace libcoro

#endif // !IO_SERVICE_HPP
