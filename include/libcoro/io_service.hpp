#ifndef IO_SERVICE_HPP
#define IO_SERVICE_HPP

#include "concepts/executor.hpp"
#include "libcoro/event_fd.hpp"
#include "libcoro/poll.hpp"
#include "libcoro/task.hpp"
#include <atomic>
#include <coroutine>
#include <format>
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
#ifdef __APPLE__
  using event_struct = struct kevent;
#elif __linux__
  using event_struct = struct epoll_event;
#endif

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
        _io_service._scheduler_event_fd.trigger();
      }
    }
    void await_resume() noexcept {}

  private:
    IOService& _io_service;
  };

  Awaiter schedule() { return Awaiter{*this}; }
  void execute(Task<void>&& task);
  void close();

  Task<detail::PollStatus> poll(int fd, detail::PollType poll_type);

  std::size_t size() const noexcept { return _awaiting_size.load(std::memory_order_acquire); }

private:
  void background_thread_function();
  void process_scheduled_tasks();
  void process_poll_event(detail::Poll*, detail::PollStatus, event_struct*);
#ifdef __APPLE__
  detail::PollStatus flag_to_poll_status(u_short flags);
#elif __linux__
  detail::PollStatus event_to_poll_status(uint32_t events);
#endif

  std::thread _io_thread;
  executor_ptr _executor{nullptr};

  int _poll_fd{-1};
  std::array<struct kevent, 16> _events{};

  detail::EventFD _scheduler_event_fd{};
  detail::EventFD _wake_up_event_fd{};
  std::atomic<bool> _scheduler_event_fd_triggered{false};

  std::mutex _awaiting_coroutines_mutex{};
  std::vector<std::coroutine_handle<>> _awaiting_coroutines{};

  std::vector<std::coroutine_handle<>> _handles_to_resume{};

  std::atomic<std::size_t> _awaiting_size{0};

  std::atomic<bool> _close_requested{false};
};
} // namespace libcoro

namespace libcoro {
template <concepts::executor Executor>
IOService<Executor>::IOService(IOService::executor_ptr executor)
#ifdef __APPLE__
    : _poll_fd(::kqueue()), _executor(executor) {
#elif __linux__
    : _poll_fd(::epoll_create1(0)), _executor(executor) {
#endif
  if (_poll_fd == -1) {
    throw std::runtime_error("Failed to create kqueue");
  }

#ifdef __APPLE__
  struct kevent event[2];
  EV_SET(&event[0], _scheduler_event_fd.read_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
  EV_SET(&event[1], _wake_up_event_fd.read_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
  ::kevent(_poll_fd, event, 2, nullptr, 0, nullptr);
#elif __linux__
  // clang-format off
  struct epoll_event event{};
  event.events = EPOLLIN;

  event.data.fd = _scheduler_event_fd.event_fd;
  ::epoll_ctl(_poll_fd, EPOLL_CTL_ADD, _scheduler_event_fd.event_fd, &event);

  event.data.fd = _wake_up_event_fd.event_fd;
  ::epoll_ctl(_poll_fd, EPOLL_CTL_ADD, _wake_up_event_fd.event_fd, &event);
  // clang-format on
#endif

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
Task<detail::PollStatus> IOService<Executor>::poll(int fd, detail::PollType poll_type) {
  _awaiting_size.fetch_add(1, std::memory_order_release);

  detail::Poll poll{};
  poll.set_fd(fd);

#ifdef __APPLE__
  struct kevent event[1];
  EV_SET(&event[0], fd, static_cast<short>(poll_type), EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0,
         &poll);

  ::kevent(_poll_fd, event, 1, nullptr, 0, nullptr);
#elif __linux__
  struct epoll_event event {};
  event.events = static_cast<uint32_t>(poll_type) | EPOLLONESHOT | EPOLLRDHUP;
  event.data.ptr = &poll;
  if (::epoll_ctl(_poll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
    throw std::runtime_error(std::format("epoll_ctl failed on fd {}", fd));
  }
#endif

  auto result = co_await poll;
  _awaiting_size.fetch_sub(1, std::memory_order_release);
  co_return result;
}

template <concepts::executor Executor>
void IOService<Executor>::process_poll_event(detail::Poll* poll, detail::PollStatus status,
                                             event_struct* event) {
  if (!poll->processed()) {
    std::atomic_thread_fence(std::memory_order_acquire);
    poll->set_processed(true);

    if (poll->fd() != -1) {
#ifdef __APPLE__
      event->flags = EV_DELETE;
      ::kevent(_poll_fd, event, 1, nullptr, 0, nullptr);
#elif __linux__
      ::epoll_ctl(_poll_fd, EPOLL_CTL_DEL, poll.fd(), nullptr);
#endif
    }

    poll->set_status(status);
    while (poll->waiting_coroutine()) {
      std::atomic_thread_fence(std::memory_order_acquire);
    }

    _handles_to_resume.push_back(poll->waiting_coroutine());
  }
}

#ifdef __APPLE__
template <concepts::executor Executor>
detail::PollStatus IOService<Executor>::flag_to_poll_status(u_short flags) {
  if (flags & EV_EOF) {
    return detail::PollStatus::EVENT_CLOSED;
  } else if (flags & EV_ERROR) {
    return detail::PollStatus::EVENT_ERROR;
  } else {
    return detail::PollStatus::EVENT_READY;
  }
}
#elif __linux__
template <concepts::executor Executor>
detail::PollStatus IOService<Executor>::event_to_poll_status(uint32_t events) {
  if (events & EPOLLRDHUP) {
    return detail::PollStatus::EVENT_CLOSED;
  } else if (events & EPOLLERR) {
    return detail::PollStatus::EVENT_ERROR;
  } else if (events & EPOLLIN || event & EPOLLOUT) {
    return detail::PollStatus::EVENT_READY;
  }

  throw std::runtime_error("Unknown event");
}
#endif

template <concepts::executor Executor>
void IOService<Executor>::background_thread_function() {
  while (!_close_requested.load(std::memory_order_acquire) || size() > 0) {
#ifdef __APPLE__
    int nevents = ::kevent(_poll_fd, nullptr, 0, _events.data(), 16, nullptr);
    if (nevents == -1) {
      throw std::runtime_error("Failed to kevent");
    }
#elif __linux__
    auto nevents = ::epoll_wait(_poll_fd, _events.data(), 16, -1);
#endif
    if (nevents > 0) {
      for (int i = 0; i < nevents; ++i) {
#ifdef __APPLE__
        if (_events[i].ident == _scheduler_event_fd.read_fd) {
#elif __linux__
        if (_events[i].data.fd == _scheduler_event_fd.event_fd) {
#endif
          process_scheduled_tasks();
#ifdef __APPLE__
        } else if (_events[i].ident == _wake_up_event_fd.read_fd) [[unlikely]] {
#elif __linux__
        } else if (_events[i].data.fd == _wake_up_event_fd.event_fd) {
#endif
          // do nothing, just wake up the kevent.
        } else {
#ifdef __APPLE__
          process_poll_event(static_cast<detail::Poll*>(_events[i].udata),
                             flag_to_poll_status(_events[i].flags), &_events[i]);
#elif __linux__
          process_poll_event(static_cast<detail::Poll*>(_events[i].data.ptr),
                             event_to_poll_status(_events[i].events), &_events[i]);
#endif
        }
      }
    }
    if (!_handles_to_resume.empty()) {
      for (auto handle : _handles_to_resume) {
        _executor->resume(handle);
      }
      _handles_to_resume.clear();
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
} // namespace libcoro

#endif // !IO_SERVICE_HPP
