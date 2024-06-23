#include "libcoro/multi_thread_executor.hpp"
#include <atomic>

namespace libcoro {
MultiThreadExecutor::MultiThreadExecutor(std::size_t size): _size(size) {
  _threads.reserve(_size);
  for (std::size_t i = 0; i < _size; ++i) {
    _threads.emplace_back([this, i] { thread_function(i); });
  }
}

MultiThreadExecutor::~MultiThreadExecutor() { shutdown(); }

MultiThreadExecutor::Awaiter MultiThreadExecutor::start() {
  if (!_shutdown_requested.load(std::memory_order_acquire)) {
    _size.fetch_add(1, std::memory_order_release);
    return Awaiter{*this};
  }

  throw std::runtime_error("Cannot start a coroutine on a shutdown executor");
}

void MultiThreadExecutor::resume(std::coroutine_handle<> handle) {
  if (!handle) {
    return;
  }
  _size.fetch_add(1, std::memory_order_release);
  execute(handle);
}

void MultiThreadExecutor::shutdown() {
  if (_shutdown_requested.exchange(true, std::memory_order_acq_rel) == false) {
    {
      std::scoped_lock lock(_wait_mutex);
      _wait_cv.notify_all();
    }

    for (auto& thread : _threads) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }
}

void MultiThreadExecutor::execute(std::coroutine_handle<> handle) {
  if (!handle) {
    return;
  }
  {
    std::scoped_lock lock(_wait_mutex);
    _handles.push_back(handle);
  }
  _wait_cv.notify_one();
}

void MultiThreadExecutor::thread_function(std::size_t idx) {
  while (!_shutdown_requested.load(std::memory_order_acquire) ||
         _size.load(std::memory_order_acquire) > 0) {
    std::unique_lock lock(_wait_mutex);
    _wait_cv.wait(lock, [&] {
      return _size.load(std::memory_order_acquire) ||
             _shutdown_requested.load(std::memory_order_acquire);
    });

    while (!_handles.empty()) {
      auto handle = _handles.front();
      _handles.pop_front();

      lock.unlock();
      handle.resume();
      _size.fetch_sub(1, std::memory_order_release);
      lock.lock();
    }
  }
}
} // namespace libcoro
