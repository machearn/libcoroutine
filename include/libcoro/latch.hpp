#ifndef LATCH_HPP
#define LATCH_HPP

#include <coroutine>
#include <mutex>

namespace libcoro {
class Latch {
public:
  explicit Latch(std::size_t count): _count(count) {}

  class Awaiter {
  public:
    explicit Awaiter(Latch& latch) noexcept: _latch(latch) {}
    bool await_ready() const noexcept { return _latch._count.load(std::memory_order_acquire) == 0; }
    bool await_suspend(std::coroutine_handle<> coroutine_handle) noexcept {
      std::size_t count = _latch._count.load(std::memory_order_acquire);
      if (count == 0) {
        return false;
      }
      _latch._coroutine_handle = coroutine_handle;
      return true;
    }
    void await_resume() noexcept {}

  private:
    Latch& _latch;
  };

  void count_down() {
    if (_count.fetch_sub(1, std::memory_order_acq_rel) <= 1) {
      _coroutine_handle.resume();
    }
  }

  Awaiter operator co_await() noexcept { return Awaiter{*this}; }

private:
  std::atomic<std::size_t> _count;
  std::coroutine_handle<> _coroutine_handle;
};
} // namespace libcoro

#endif // !LATCH_HPP
