#ifndef IO_SERVICE_HPP
#define IO_SERVICE_HPP

#include "concepts/executor.hpp"
#include "libcoro/event_fd.hpp"
#include "libcoro/task.hpp"
#include <atomic>
#include <coroutine>
#include <mutex>
#include <sys/event.h>
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/event.h>
#elif __linux__
#include <sys/epoll.h>
#endif

namespace libcoro {
template <concepts::executor Executor>
class IOService {
  using executor_ptr = std::shared_ptr<Executor>;

public:
  IOService(executor_ptr);
  ~IOService();

  IOService(const IOService&) = delete;
  IOService& operator=(const IOService&) = delete;
  IOService(IOService&&) = delete;
  IOService& operator=(IOService&&) = delete;

  class Awaiter {
    friend class IOService;
    explicit Awaiter(IOService& io_service) noexcept: _io_service(io_service) {}

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

  Awaiter schedule() { return Awaiter{*this}; }
  void execute(Task<void>&& task);
  void close();

  std::size_t size() const noexcept { return _awaiting_size.load(std::memory_order_acquire); }

private:
  void background_thread_function();
  void process_scheduled_tasks();

  std::thread _io_thread;
  executor_ptr _executor{nullptr};

  int _poll_fd{-1};
#ifdef __APPLE__
  detail::EventFD _scheduler_event_fd{};
  detail::EventFD _wake_up_event_fd{};
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

namespace libcoro {
#ifdef __APPLE__
template <concepts::executor Executor>
IOService<Executor>::IOService(IOService::executor_ptr executor)
    : _poll_fd(::kqueue()), _executor(executor) {
  if (_poll_fd == -1) {
    throw std::runtime_error("Failed to create kqueue");
  }

  struct kevent event[1];
  EV_SET(&event[0], _scheduler_event_fd.read_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
  ::kevent(_poll_fd, event, 1, nullptr, 0, nullptr);

  _io_thread = std::thread([this]() { background_thread_function(); });
}

template <concepts::executor Executor>
IOService<Executor>::~IOService() {
  close();
}

template <concepts::executor Executor>
void IOService<Executor>::close() {
  while (_close_requested.exchange(true, std::memory_order_acq_rel) == false) {
    _executor->shutdown();

    _wake_up_event_fd.trigger();

    if (_io_thread.joinable()) {
      _io_thread.join();
    }

    if (_poll_fd != -1) {
      ::close(_poll_fd);
      _poll_fd = -1;
    }

    _scheduler_event_fd.close();
    _wake_up_event_fd.close();
  }
}

template <concepts::executor Executor>
void IOService<Executor>::background_thread_function() {
  while (!_close_requested.load(std::memory_order_acquire) || size() > 0) {
    int nevents = ::kevent(_poll_fd, nullptr, 0, _events.data(), 16, nullptr);
    if (nevents == -1) {
      throw std::runtime_error("Failed to kevent");
    }
    if (nevents > 0) {
      for (int i = 0; i < nevents; ++i) {
        if (_events[i].ident == _scheduler_event_fd.read_fd) {
          process_scheduled_tasks();
        } else if (_events[i].ident == _wake_up_event_fd.read_fd) {
          // do nothing, just wake up the kevent.
        }
      }
    }
  }
}

template <concepts::executor Executor>
void IOService<Executor>::process_scheduled_tasks() {
  std::vector<std::coroutine_handle<>> coroutines;
  {
    std::scoped_lock lock(_awaiting_coroutines_mutex);
    coroutines.swap(_awaiting_coroutines);

    _scheduler_event_fd.reset();
    _scheduler_event_fd_triggered.store(false, std::memory_order_release);
  }
  for (auto coroutine : coroutines) {
    _executor->resume(coroutine);
  }
  _awaiting_size.fetch_sub(coroutines.size(), std::memory_order_release);
}
#elif __linux__
// clang-format off

// clang-format on
#endif
} // namespace libcoro

#endif // !IO_SERVICE_HPP
