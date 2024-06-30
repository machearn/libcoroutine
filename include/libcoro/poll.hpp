#ifndef POLL_HPP
#define POLL_HPP

#include <atomic>
#include <coroutine>

#ifdef __APPLE__
#include <sys/event.h>
#elif __linux__
#include <sys/epoll.h>
#endif

namespace libcoro {
namespace detail {

enum class PollStatus { EVENT_READY, EVENT_TIMEOUT, EVENT_ERROR, EVENT_CLOSED };
#ifdef __APPLE__
enum class PollType {
  READ = EVFILT_READ,
  WRITE = EVFILT_WRITE,
};
#elif __linux__
// clang-format off
enum class PollType {
  READ = EPOLLIN,
  WRITE = EPOLLOUT,
  READ_WRITE = EPOLLIN | EPOLLOUT
};
// clang-format on
#endif

class Poll {
public:
  Poll() = default;
  ~Poll() = default;

  Poll(const Poll&) = delete;
  Poll& operator=(const Poll&) = delete;
  Poll(Poll&&) = delete;
  Poll& operator=(Poll&&) = delete;

  class PollAwaiter {
  public:
    PollAwaiter(Poll& poll) noexcept: _poll(poll) {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> waiting_coroutine) noexcept {
      _poll.set_waiting_coroutine(waiting_coroutine);
      std::atomic_thread_fence(std::memory_order_release);
    }
    PollStatus await_resume() const noexcept { return _poll.status(); }

  private:
    Poll& _poll;
  };

  PollAwaiter operator co_await() noexcept { return PollAwaiter{*this}; }

  bool processed() const noexcept { return _processed; }
  void set_processed(bool processed) { _processed = processed; }

  int fd() const noexcept { return _fd; }
  void set_fd(int fd) noexcept { _fd = fd; }

  PollStatus status() const noexcept { return _status; }
  void set_status(PollStatus status) { _status = status; }

  std::coroutine_handle<> waiting_coroutine() const noexcept { return _waiting_coroutine; }
  void set_waiting_coroutine(std::coroutine_handle<> waiting_coroutine) noexcept {
    _waiting_coroutine = waiting_coroutine;
  }

private:
  int _fd{-1};
  std::coroutine_handle<> _waiting_coroutine{nullptr};
  PollStatus _status{PollStatus::EVENT_CLOSED};

  bool _processed{false};
};

} // namespace detail
} // namespace libcoro

#endif // !POLL_HPP
