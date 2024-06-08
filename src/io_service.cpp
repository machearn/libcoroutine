#include "libcoro/io_service.hpp"
#include <unistd.h>

#ifdef __APPLE__
#include <sys/event.h>
#elif __linux__
#include <sys/epoll.h>
#endif

namespace libcoro {
#ifdef __APPLE__
IOService::IOService(): _poll_fd(::kqueue()) {
  if (_poll_fd == -1) {
    throw std::runtime_error("Failed to create kqueue");
  }

  struct kevent event[1];
  EV_SET(&event[0], _scheduler_event_fd.read_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
  ::kevent(_poll_fd, event, 1, nullptr, 0, nullptr);

  _io_thread = std::thread([this]() { process_tasks_background(); });
}

IOService::~IOService() {
  _close_requested.store(true, std::memory_order_release);

  if (_io_thread.joinable()) {
    _io_thread.join();
  }

  if (_poll_fd != -1) {
    ::close(_poll_fd);
  }

  _scheduler_event_fd.close();
}

void IOService::process_tasks_background() {
  while (!_close_requested.load(std::memory_order_acquire) || size() > 0) {
    int nevents = ::kevent(_poll_fd, nullptr, 0, _events.data(), 16, nullptr);
    if (nevents == -1) {
      throw std::runtime_error("Failed to kevent");
    }
    if (nevents > 0) {
      for (int i = 0; i < nevents; ++i) {
        if (_events[i].ident == _scheduler_event_fd.read_fd) {
          process_scheduled_tasks();
        }
      }
    }
  }
}

void IOService::process_scheduled_tasks() {
  std::vector<std::coroutine_handle<>> coroutines;
  {
    std::scoped_lock lock(_awaiting_coroutines_mutex);
    coroutines.swap(_awaiting_coroutines);

    _scheduler_event_fd.reset();
    _scheduler_event_fd_triggered.store(false, std::memory_order_release);
  }
  for (auto coroutine : coroutines) {
    coroutine.resume();
  }
  _awaiting_size.fetch_sub(coroutines.size(), std::memory_order_release);
}
#elif __linux__
// clang-format off

// clang-format on
#endif
} // namespace libcoro
