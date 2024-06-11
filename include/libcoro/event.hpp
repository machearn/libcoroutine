#ifndef EVENT_HPP
#define EVENT_HPP

#include <atomic>
#include <coroutine>

namespace libcoro {
class Event {
public:
  class Awaiter {
    friend class Event;

  public:
    explicit Awaiter(const Event& event) noexcept: _event(event) {}

    bool await_ready() const noexcept { return _event.is_triggered(); }
    bool await_suspend(std::coroutine_handle<> coroutine_handle) noexcept;
    void await_resume() noexcept {}

  private:
    const Event& _event;
    std::coroutine_handle<> _coroutine_handle;
    Awaiter* _next;
  };

  void trigger() noexcept;
  void reset() noexcept {
    _triggered.store(false, std::memory_order_release);
    _awaiting.store(nullptr, std::memory_order_release);
  }
  bool is_triggered() const noexcept { return _triggered.load(std::memory_order_acquire); }

private:
  friend class Awaiter;

  std::atomic<bool> _triggered{false};
  mutable std::atomic<Awaiter*> _awaiting{nullptr};
};
} // namespace libcoro

#endif // !EVENT_HPP
