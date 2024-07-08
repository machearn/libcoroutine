#ifndef ALL_HPP
#define ALL_HPP

#include "concepts/awaitable.hpp"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <exception>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace libcoro {
namespace detail {
class AllLatch {
public:
  AllLatch(std::size_t count): _count(count + 1), _awaiting_coroutine(nullptr) {}

  AllLatch(const AllLatch&) = delete;
  AllLatch& operator=(const AllLatch&) = delete;

  AllLatch(AllLatch&& other) noexcept
      : _count(other._count.exchange(0, std::memory_order_acq_rel)),
        _awaiting_coroutine(std::exchange(other._awaiting_coroutine, nullptr)) {}
  AllLatch& operator=(AllLatch&& other) noexcept {
    if (this != &other) {
      _count.store(other._count.exchange(0, std::memory_order_acq_rel), std::memory_order_release);
      _awaiting_coroutine = std::exchange(other._awaiting_coroutine, nullptr);
    }
    return *this;
  }

  bool is_ready() const noexcept {
    return _awaiting_coroutine != nullptr && _awaiting_coroutine.done();
  }

  bool try_wait(std::coroutine_handle<> awaiting_coroutine) noexcept {
    _awaiting_coroutine = awaiting_coroutine;
    return _count.fetch_sub(1, std::memory_order_acq_rel) > 1;
  }

  void notify_completed() noexcept {
    if (_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      if (_awaiting_coroutine) {
        _awaiting_coroutine.resume();
      }
    }
  }

private:
  std::atomic<std::size_t> _count;
  std::coroutine_handle<> _awaiting_coroutine;
};

template <typename T>
class AllAwaitable;

template <typename T>
class AllTask;

template <>
class AllAwaitable<std::tuple<>> {
public:
  constexpr AllAwaitable() noexcept = default;
  explicit constexpr AllAwaitable(std::tuple<>) noexcept {}

  constexpr bool await_ready() const noexcept { return true; }
  constexpr void await_suspend(std::coroutine_handle<>) noexcept {}
  constexpr std::tuple<> await_resume() noexcept { return {}; }
};

template <typename... Ts>
class AllAwaitable<std::tuple<Ts...>> {
  class awaiter_base {
  public:
    explicit awaiter_base(AllAwaitable& awaitable) noexcept: _awaitable(awaitable) {}

    bool await_ready() const noexcept { return _awaitable.is_ready(); }

    bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept {
      return _awaitable.try_wait(awaiting_coroutine);
    }

  protected:
    AllAwaitable& _awaitable;
  };

public:
  explicit AllAwaitable(Ts&&... tasks) noexcept(
      std::conjunction<std::is_nothrow_move_constructible<Ts>...>::value)
      : _latch(sizeof...(Ts)), _tasks(std::move<Ts>(tasks)...) {}

  explicit AllAwaitable(std::tuple<Ts...>&& tasks) noexcept(
      std::is_nothrow_move_constructible_v<std::tuple<Ts...>>)
      : _latch(sizeof...(Ts)), _tasks(std::move(tasks)) {}

  AllAwaitable(const AllAwaitable&) = delete;
  AllAwaitable& operator=(const AllAwaitable&) = delete;

  AllAwaitable(AllAwaitable&& other)
      : _latch(std::move(other._latch)), _tasks(std::move(other._tasks)) {}
  AllAwaitable& operator=(AllAwaitable&&) = delete;

  auto operator co_await() & noexcept {
    class awaiter: public awaiter_base {
    public:
      std::tuple<Ts...>& await_resume() noexcept { return this->_awaitable._tasks; }
    };

    return awaiter{*this};
  }

  auto operator co_await() && noexcept {
    class awaiter: public awaiter_base {
    public:
      std::tuple<Ts...>&& await_resume() noexcept { return std::move(this->_awaitable._tasks); }
    };

    return awaiter{*this};
  }

private:
  bool is_ready() const noexcept { return _latch.is_ready(); }
  bool try_wait(std::coroutine_handle<> awaiting_coroutine) noexcept {
    std::apply([this](auto&&... tasks) { ((tasks.start(_latch)), ...); }, _tasks);
    return _latch.try_wait(awaiting_coroutine);
  }

  std::tuple<Ts...> _tasks;
  AllLatch _latch;
};

template <typename T>
class AllAwaitable {
  class awaiter_base {
  public:
    explicit awaiter_base(AllAwaitable& awaitable) noexcept: _awaitable(awaitable) {}

    bool await_ready() const noexcept { return _awaitable.is_ready(); }

    bool await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept {
      return _awaitable.try_wait(awaiting_coroutine);
    }

  protected:
    AllAwaitable& _awaitable;
  };

public:
  explicit AllAwaitable(T&& tasks) noexcept
      : _latch(std::size(tasks)), _tasks(std::forward<T>(tasks)) {}

  AllAwaitable(const AllAwaitable&) = delete;
  AllAwaitable& operator=(const AllAwaitable&) = delete;

  AllAwaitable(AllAwaitable&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : _latch(std::move(other._latch)), _tasks(std::move(other._tasks)) {}
  AllAwaitable& operator=(AllAwaitable&&) = delete;

  auto operator co_await() & noexcept {
    class awaiter: public awaiter_base {
    public:
      T& await_resume() noexcept { return this->_awaitable._tasks; }
    };

    return awaiter{*this};
  }

  auto operator co_await() && noexcept {
    class awaiter: public awaiter_base {
    public:
      T&& await_resume() noexcept { return std::move(this->_awaitable._tasks); }
    };

    return awaiter{*this};
  }

private:
  bool is_ready() const noexcept { return _latch.is_ready(); }
  bool try_wait(std::coroutine_handle<> awaiting_coroutine) noexcept {
    for (auto& task : _tasks) {
      task.start(_latch);
    }
    return _latch.try_wait(awaiting_coroutine);
  }

  T _tasks;
  AllLatch _latch;
};
template <typename T>
class AllPromise {
public:
  using coroutine_handle_type = std::coroutine_handle<AllPromise<T>>;
  AllPromise() noexcept = default;

  auto get_return_object() noexcept { return coroutine_handle_type::from_promise(*this); }

  std::suspend_always initial_suspend() noexcept { return {}; }

  auto final_suspend() noexcept {
    class awaiter {
    public:
      bool await_ready() const noexcept { return false; }
      void await_suspend(coroutine_handle_type coroutine_handle) noexcept {
        coroutine_handle.promise()._latch->notify_completed();
      }
      void await_resume() noexcept {}
    };
    return awaiter{*this};
  }

  auto unhandled_exception() noexcept { _exception = std::current_exception(); }

  auto yield_value(T&& result) noexcept {
    _result = std::addressof(result);
    return final_suspend();
  }

  auto start(AllLatch& latch) noexcept {
    _latch = &latch;
    coroutine_handle_type::from_promise(*this).resume();
  }

  T& result() & noexcept {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
    return *_result;
  }

  T&& result() && noexcept {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
    return std::forward(*_result);
  }

  auto return_void() noexcept { assert(false); }

private:
  AllLatch* _latch{nullptr};
  std::exception_ptr _exception;
  std::add_pointer<T> _result{nullptr};
};

template <>
class AllPromise<void> {
public:
  using coroutine_handle_type = std::coroutine_handle<AllPromise<void>>;
  AllPromise() noexcept = default;

  auto get_return_object() noexcept { return coroutine_handle_type::from_promise(*this); }

  std::suspend_always initial_suspend() noexcept { return {}; }

  auto final_suspend() noexcept {
    class awaiter {
    public:
      bool await_ready() const noexcept { return false; }
      void await_suspend(coroutine_handle_type coroutine_handle) noexcept {
        coroutine_handle.promise()._latch->notify_completed();
      }
      void await_resume() noexcept {}
    };
    return awaiter{};
  }

  auto unhandled_exception() noexcept { _exception = std::current_exception(); }

  void return_void() noexcept {}

  void result() const noexcept {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
  }

  void start(AllLatch& latch) noexcept {
    _latch = &latch;
    coroutine_handle_type::from_promise(*this).resume();
  }

private:
  AllLatch* _latch{nullptr};
  std::exception_ptr _exception;
};

struct void_value {};

template <typename T>
class AllTask {
public:
  template <typename TaskContainer>
  friend class AllAwaitable;

  using promise_type = AllPromise<T>;
  using coroutine_handle_type = typename promise_type::coroutine_handle_type;

  AllTask(coroutine_handle_type coroutine_handle) noexcept: _coroutine_handle(coroutine_handle) {}

  AllTask(const AllTask&) = delete;
  AllTask& operator=(const AllTask&) = delete;

  AllTask(AllTask&& other) noexcept
      : _coroutine_handle(std::exchange(other._coroutine_handle, coroutine_handle_type{nullptr})) {}
  AllTask& operator=(AllTask&&) = delete;

  ~AllTask() {
    if (_coroutine_handle) {
      _coroutine_handle.destroy();
    }
  }

  auto return_value() & -> decltype(auto) {
    if constexpr (std::is_void_v<T>) {
      _coroutine_handle.promise().result();
      return void_value{};
    } else {
      return _coroutine_handle.promise().result();
    }
  }

  auto return_value() const& -> decltype(auto) {
    if constexpr (std::is_void_v<T>) {
      _coroutine_handle.promise().result();
      return void_value{};
    } else {
      return _coroutine_handle.promise().result();
    }
  }

  auto return_value() && -> decltype(auto) {
    if constexpr (std::is_void_v<T>) {
      _coroutine_handle.promise().result();
      return void_value{};
    } else {
      return std::move(_coroutine_handle.promise().result());
    }
  }

private:
  void start(AllLatch& latch) noexcept { _coroutine_handle.promise().start(latch); }
  coroutine_handle_type _coroutine_handle;
};

template <concepts::awaitable awaitable_t,
          typename return_t = typename concepts::awaitable_traits<awaitable_t>::awaiter_return_t>
static AllTask<return_t> make_all_task(awaitable_t awaitable);

template <concepts::awaitable awaitable_t, typename return_t>
static AllTask<return_t> make_all_task(awaitable_t awaitable) {
  if constexpr (std::is_void_v<return_t>) {
    co_await static_cast<awaitable_t&&>(awaitable);
    co_return;
  } else {
    co_yield co_await static_cast<awaitable_t&&>(awaitable);
  }
}
} // namespace detail

template <concepts::awaitable... awaitables_t>
[[nodiscard]] auto all(awaitables_t... awaitables) {
  return detail::AllAwaitable<std::tuple<
      detail::AllTask<typename concepts::awaitable_traits<awaitables_t>::awaiter_return_t>...>>(
      std::make_tuple(detail::make_all_task(std::move(awaitables))...));
}

template <std::ranges::range range_t,
          concepts::awaitable awaitable_t = typename std::ranges::range_value_t<range_t>,
          typename return_t = typename concepts::awaitable_traits<awaitable_t>::awaiter_return_t>
[[nodiscard]] auto all(range_t&& awaitables) {
  std::vector<detail::AllTask<return_t>> tasks;
  if constexpr (std::ranges::sized_range<range_t>) {
    tasks.reserve(std::ranges::size(awaitables));
  }

  for (auto&& a : awaitables) {
    tasks.emplace_back(detail::make_all_task(std::move(a)));
  }

  return detail::AllAwaitable<std::vector<detail::AllTask<return_t>>>(std::move(tasks));
}
} // namespace libcoro

#endif // !ALL_HPP
