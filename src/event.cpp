#include "libcoro/event.hpp"

namespace libcoro {
void Event::trigger() noexcept {
  _triggered.store(true, std::memory_order_release);
  Awaiter* awaiter = _awaiting.exchange(nullptr, std::memory_order_acq_rel);
  while (awaiter != nullptr) {
    auto* next = awaiter->_next;
    awaiter->_next = nullptr;
    awaiter->_coroutine_handle.resume();
    awaiter = next;
  }
}

bool Event::Awaiter::await_suspend(std::coroutine_handle<> coroutine_handle) noexcept {
  _coroutine_handle = coroutine_handle;
  Awaiter* next_awaiter = _event._awaiting.load(std::memory_order_acquire);

  do {
    if (_event.is_triggered()) {
      return false;
    }

    _next = next_awaiter;
  } while (!_event._awaiting.compare_exchange_weak(next_awaiter, this, std::memory_order_release,
                                                   std::memory_order_acquire));

  return true;
}
} // namespace libcoro
