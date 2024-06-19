#ifndef SYNC_HPP
#define SYNC_HPP

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "concepts/awaitable.hpp"

namespace libcoro {
namespace detail {
struct unset_return_value {
  unset_return_value() = default;
  unset_return_value(unset_return_value const&) = delete;
  unset_return_value& operator=(unset_return_value const&) = delete;
  unset_return_value(unset_return_value&&) = delete;
  unset_return_value& operator=(unset_return_value&&) = delete;
};

class SyncEvent {
public:
  SyncEvent() noexcept = default;
  ~SyncEvent() noexcept = default;

  SyncEvent(const SyncEvent&) = delete;
  SyncEvent& operator=(const SyncEvent&) = delete;

  SyncEvent(SyncEvent&&) = delete;
  SyncEvent& operator=(SyncEvent&&) = delete;

  void trigger() {
    _triggered.store(true, std::memory_order_release);
    _cv.notify_all();
  }

  void reset() { _triggered.store(false, std::memory_order_release); }

  void wait() {
    std::unique_lock<std::mutex> lock(_mutex);
    _cv.wait(lock, [this] { return _triggered.load(std::memory_order_acquire); });
  }

private:
  std::atomic<bool> _triggered{false};
  std::mutex _mutex;
  std::condition_variable _cv;
};

class SyncTaskPromiseBase {
public:
  SyncTaskPromiseBase() noexcept = default;

  std::suspend_always initial_suspend() noexcept { return {}; }

  void unhandled_exception() noexcept { _exception = std::current_exception(); }

protected:
  virtual ~SyncTaskPromiseBase() noexcept = default;

  SyncEvent* _event{nullptr};
  std::exception_ptr _exception;
};

template <typename T>
class SyncTaskPromise final: public SyncTaskPromiseBase {
public:
  using coroutine_handle_type = std::coroutine_handle<SyncTaskPromise<T>>;
  static constexpr bool T_is_reference = std::is_reference_v<T>;
  using unqualified_T =
      std::conditional_t<T_is_reference, std::remove_reference_t<T>*, std::remove_const_t<T>>;
  using result_type = std::variant<detail::unset_return_value, unqualified_T, std::exception_ptr>;

  SyncTaskPromise() noexcept = default;
  ~SyncTaskPromise() noexcept = default;

  SyncTaskPromise(const SyncTaskPromise&) = delete;
  SyncTaskPromise& operator=(const SyncTaskPromise&) = delete;
  SyncTaskPromise(SyncTaskPromise&&) = delete;
  SyncTaskPromise& operator=(SyncTaskPromise&&) = delete;

  void start(SyncEvent& event) noexcept {
    _event = &event;
    coroutine_handle_type::from_promise(*this).resume();
  }

  auto get_return_object() noexcept { return coroutine_handle_type::from_promise(*this); }

  template <typename U>
    requires(T_is_reference and std::is_constructible_v<T, U &&>) or
            (!T_is_reference and std::is_constructible_v<unqualified_T, U &&>)
  void return_value(U&& value) noexcept {
    if constexpr (T_is_reference) {
      T ref = static_cast<U&&>(value);
      _result.template emplace<unqualified_T>(std::addressof(ref));
    } else {
      _result.template emplace<unqualified_T>(std::forward<U>(value));
    }
  }

  void return_value(unqualified_T value) noexcept
    requires(!T_is_reference)
  {
    if constexpr (std::is_move_constructible_v<unqualified_T>) {
      _result.template emplace<unqualified_T>(std::move(value));
    } else {
      _result.template emplace<unqualified_T>(value);
    }
  }

  auto final_suspend() noexcept {
    struct Awaiter {
      bool await_ready() const noexcept { return false; }
      void await_suspend(coroutine_handle_type handle) noexcept {
        auto& promise = handle.promise();
        if (promise._event != nullptr) {
          promise._event->trigger();
        }
      }
      void await_resume() noexcept {}
    };

    return Awaiter{};
  }

  auto result() & -> decltype(auto) {
    if (std::holds_alternative<unqualified_T>(_result)) {
      if constexpr (T_is_reference) {
        return static_cast<T>(*std::get<unqualified_T>(_result));
      } else {
        return static_cast<const T&>(std::get<unqualified_T>(_result));
      }
    } else if (std::holds_alternative<std::exception_ptr>(_result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(_result));
    } else {
      throw std::runtime_error("Task<T> result accessed before task completion");
    }
  }

  auto result() const& -> decltype(auto) {
    if (std::holds_alternative<unqualified_T>(_result)) {
      if constexpr (T_is_reference) {
        return static_cast<std::add_const_t<T>>(*std::get<unqualified_T>(_result));
      } else {
        return static_cast<const T&>(std::get<unqualified_T>(_result));
      }
    } else if (std::holds_alternative<std::exception_ptr>(_result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(_result));
    } else {
      throw std::runtime_error("Task<T> result accessed before task completion");
    }
  }

  auto result() && -> decltype(auto) {
    if (std::holds_alternative<unqualified_T>(_result)) {
      if constexpr (T_is_reference) {
        return static_cast<T>(*std::get<unqualified_T>(_result));
      } else if constexpr (std::is_move_constructible_v<T>) {
        return static_cast<T&&>(std::get<unqualified_T>(_result));
      } else {
        return static_cast<const T&&>(std::get<unqualified_T>(_result));
      }
    } else if (std::holds_alternative<std::exception_ptr>(_result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(_result));
    } else {
      throw std::runtime_error("Task<T> result accessed before task completion");
    }
  }

private:
  result_type _result;
};

template <>
class SyncTaskPromise<void> final: public SyncTaskPromiseBase {
public:
  SyncTaskPromise() noexcept = default;
  ~SyncTaskPromise() noexcept = default;

  SyncTaskPromise(const SyncTaskPromise&) = delete;
  SyncTaskPromise& operator=(const SyncTaskPromise&) = delete;
  SyncTaskPromise(SyncTaskPromise&&) = delete;
  SyncTaskPromise& operator=(SyncTaskPromise&&) = delete;

  using coroutine_handle_type = std::coroutine_handle<SyncTaskPromise<void>>;

  void start(SyncEvent& event) noexcept {
    _event = &event;
    coroutine_handle_type::from_promise(*this).resume();
  }

  auto get_return_object() noexcept { return coroutine_handle_type::from_promise(*this); }

  auto final_suspend() noexcept {
    struct Awaiter {
      bool await_ready() const noexcept { return false; }
      void await_suspend(coroutine_handle_type handle) noexcept {
        auto& promise = handle.promise();
        if (promise._event != nullptr) {
          promise._event->trigger();
        }
      }
      void await_resume() noexcept {}
    };

    return Awaiter{};
  }

  void return_void() noexcept {}
  void unhandled_exception() noexcept { std::rethrow_exception(std::current_exception()); }

  void result() const noexcept {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
  }

private:
  std::exception_ptr _exception;
};

template <typename T>
class SyncTask {
public:
  using promise_type = SyncTaskPromise<T>;
  using coroutine_handle_type = std::coroutine_handle<promise_type>;

  SyncTask(coroutine_handle_type coroutine): _coroutine(coroutine){};
  ~SyncTask() {
    if (_coroutine) {
      _coroutine.destroy();
    }
  }
  SyncTask(SyncTask&& other) noexcept
      : _coroutine(std::exchange(other._coroutine, coroutine_handle_type{})) {}
  SyncTask& operator=(SyncTask&& other) noexcept {
    if (std::addressof(other) != this) {
      _coroutine = std::exchange(other._coroutine, coroutine_handle_type{});
    }

    return *this;
  }

  SyncTask(const SyncTask&) = delete;
  SyncTask& operator=(const SyncTask&) = delete;

  promise_type& promise() & { return _coroutine.promise(); }
  const promise_type& promise() const& { return _coroutine.promise(); }
  promise_type&& promise() && { return std::move(_coroutine.promise()); }

private:
  coroutine_handle_type _coroutine;
};

template <concepts::awaitable awaitable_t,
          typename return_t = concepts::awaitable_traits<awaitable_t>::awaiter_return_t>
static SyncTask<return_t> make_sync_task(awaitable_t&& awaitable);

template <concepts::awaitable awaitable_t, typename return_t>
static SyncTask<return_t> make_sync_task(awaitable_t&& awaitable) {
  if constexpr (std::is_void_v<return_t>) {
    co_await std::forward<awaitable_t>(awaitable);
    co_return;
  } else {
    co_return co_await std::forward<awaitable_t>(awaitable);
  }
}
} // namespace detail

template <concepts::awaitable awaitable_t,
          typename return_t = concepts::awaitable_traits<awaitable_t>::awaiter_return_t>
auto sync(awaitable_t&& awaitable) -> decltype(auto) {
  detail::SyncEvent event{};
  auto task = detail::make_sync_task(std::forward<awaitable_t>(awaitable));
  task.promise().start(event);
  event.wait();

  if constexpr (std::is_void_v<return_t>) {
    task.promise().result();
    return;
  } else if constexpr (std::is_reference_v<return_t>) {
    return task.promise().result();
  } else if constexpr (std::is_move_assignable_v<return_t>) {
    auto result = std::move(task).promise().result();
    return result;
  } else {
    return task.promise().result();
  }
}
} // namespace libcoro

#endif // !SYNC_HPP
