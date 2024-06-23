#include "libcoro/single_thread_executor.hpp"
#include <atomic>
#include <mutex>

namespace libcoro {
SingleThreadExecutor::SingleThreadExecutor()
    : _execute_thread(&SingleThreadExecutor::background_thread, this) {}

SingleThreadExecutor::~SingleThreadExecutor() { shutdown(); }

void SingleThreadExecutor::resume(std::coroutine_handle<> handle) { execute(handle); }

void SingleThreadExecutor::execute(std::coroutine_handle<> handle) {
  if (!handle) {
    return;
  }
  {
    std::scoped_lock lock(_wait_mutex);
    _handle = handle;
  }
  _wait_cv.notify_one();
}

void SingleThreadExecutor::shutdown() {
  if (_shutdown_requested.exchange(true, std::memory_order_acq_rel) == false) {
    {
      std::scoped_lock lock(_wait_mutex);
      _wait_cv.notify_one();
    }

    if (_execute_thread.joinable()) {
      _execute_thread.join();
    }
  }
}

void SingleThreadExecutor::background_thread() {
  while (!_shutdown_requested.load(std::memory_order_acquire)) {
    std::unique_lock lock(_wait_mutex);
    _wait_cv.wait(lock, [&]() {
      return static_cast<bool>(_handle) || _shutdown_requested.load(std::memory_order_acquire);
    });

    if (_handle) {
      auto handle = _handle;
      _handle = nullptr;
      lock.unlock();
      handle.resume();
      lock.lock();
    }
  }
}
} // namespace libcoro
