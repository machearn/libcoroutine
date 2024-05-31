#ifndef LIBCORO_TASK_HPP
#define LIBCORO_TASK_HPP

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace libcoro {

template <class T = void>
class Task;

namespace detail {

class TaskPromiseBase {
public:
  friend struct FinalAwaiter;
  struct FinalAwaiter {
    constexpr bool await_ready() noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
      auto& promise = handle.promise();
      if (promise._coroutine_handle != nullptr) {
        return promise._coroutine_handle;
      } else {
        return std::noop_coroutine();
      }
    }

    void await_resume() noexcept {}
  };

  TaskPromiseBase() noexcept = default;
  virtual ~TaskPromiseBase() = default;

  std::suspend_always initial_suspend() noexcept { return {}; }
  FinalAwaiter final_suspend() noexcept { return {}; }
  void set_coroutine_handle(std::coroutine_handle<> handle) noexcept { _coroutine_handle = handle; }

private:
  std::coroutine_handle<> _coroutine_handle;
};

template <class T>
class TaskPromise final: public TaskPromiseBase {
  struct unset_result_type {
    unset_result_type() = default;
    unset_result_type(const unset_result_type&) = delete;
    unset_result_type& operator=(const unset_result_type&) = delete;
    unset_result_type(unset_result_type&&) = delete;
    unset_result_type& operator=(unset_result_type&&) = delete;
    ~unset_result_type() = default;
  };

public:
  using task_type = Task<T>;
  using coroutine_handle_type = std::coroutine_handle<TaskPromise<T>>;
  static constexpr bool T_is_reference = std::is_reference_v<T>;
  using unqualified_T =
      std::conditional_t<T_is_reference, std::remove_reference_t<T>*, std::remove_const_t<T>>;
  using result_type = std::variant<unset_result_type, unqualified_T, std::exception_ptr>;

  TaskPromise() noexcept = default;
  ~TaskPromise() = default;

  TaskPromise(const TaskPromise&) = delete;
  TaskPromise& operator=(const TaskPromise&) = delete;
  TaskPromise(TaskPromise&&) = delete;
  TaskPromise& operator=(TaskPromise&&) = delete;

  task_type get_return_object() noexcept;

  template <class U>
    requires(T_is_reference and std::is_constructible_v<T, U &&>) or
            (!T_is_reference and std::is_constructible_v<unqualified_T, U &&>)
  void return_value(U&& value) noexcept {
    if constexpr (T_is_reference) {
      T result_ref = static_cast<U&&>(value);
      _result.template emplace<unqualified_T>(std::addressof(result_ref));
    } else {
      _result.template emplace<unqualified_T>(std::forward<U>(value));
    }
  }

  void return_value(unqualified_T value)
    requires(!T_is_reference)
  {
    if constexpr (std::is_nothrow_move_constructible_v<unqualified_T>) {
      _result.template emplace<unqualified_T>(std::move(value));
    } else {
      _result.template emplace<unqualified_T>(value);
    }
  }

  void unhandled_exception() noexcept { new (&_result) result_type(std::current_exception()); }

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
  result_type _result{};
};

template <>
class TaskPromise<void> final: public TaskPromiseBase {
public:
  using task_type = Task<void>;
  using coroutine_handle_type = std::coroutine_handle<TaskPromise<void>>;

  TaskPromise() noexcept = default;
  ~TaskPromise() = default;
  TaskPromise(const TaskPromise&) = delete;
  TaskPromise& operator=(const TaskPromise&) = delete;
  TaskPromise(TaskPromise&&) = delete;
  TaskPromise& operator=(TaskPromise&&) = delete;

  task_type get_return_object() noexcept;
  void return_void() noexcept {}
  void unhandled_exception() noexcept { std::rethrow_exception(std::current_exception()); }

  void result() const noexcept {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
  }

private:
  std::exception_ptr _exception{nullptr};
};

} // namespace detail

template <class T>
class [[nodiscard]] Task {
public:
  using promise_type = detail::TaskPromise<T>;
  using coroutine_handle_type = typename promise_type::coroutine_handle_type;

  struct task_awater_base {
    task_awater_base(coroutine_handle_type handle) noexcept: _coroutine_handle(handle) {}

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(coroutine_handle_type handle) noexcept {
      handle.promise().set_coroutine_handle(handle);
      return _coroutine_handle;
    }

    coroutine_handle_type _coroutine_handle{nullptr};
  };

  Task() noexcept: _coroutine_handle(nullptr) {}
  explicit Task(coroutine_handle_type handle): _coroutine_handle(handle) {}

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  Task(Task&& other) noexcept: _coroutine_handle(std::exchange(other._coroutine_handle, nullptr)) {}
  Task& operator=(Task&& other) noexcept {
    if (this != std::addressof(other)) {
      if (_coroutine_handle) {
        _coroutine_handle.destroy();
      }
      _coroutine_handle = std::exchange(other._coroutine_handle, nullptr);
    }
    return *this;
  }

  ~Task() {
    if (_coroutine_handle) {
      _coroutine_handle.destroy();
    }
  }

  bool await_ready() const noexcept { return !_coroutine_handle || _coroutine_handle.done(); }
  bool resume() {
    if (!_coroutine_handle)
      return false;
    if (!_coroutine_handle.done()) {
      _coroutine_handle.resume();
    }
    return !_coroutine_handle.done();
  }
  bool destroy() {
    if (!_coroutine_handle)
      return false;
    _coroutine_handle.destroy();
    _coroutine_handle = nullptr;
    return true;
  }

  auto operator co_await() const& noexcept {
    struct awaiter: public task_awater_base {
      auto await_resume() -> decltype(auto) { return this->_coroutine_handle.promise().result(); }
    };
    return awaiter{_coroutine_handle};
  }

  auto operator co_await() const&& noexcept {
    struct awaiter: public task_awater_base {
      auto await_resume() -> decltype(auto) {
        return std::move(this->_coroutine_handle.promise()).result();
      }
    };
    return awaiter{_coroutine_handle};
  }

  promise_type get_promise() & { return _coroutine_handle.promise(); }
  promise_type& get_promise() const& { return _coroutine_handle.promise(); }
  promise_type&& get_promise() && { return std::move(_coroutine_handle.promise()); }

  coroutine_handle_type get_coroutine_handle() const noexcept { return _coroutine_handle; }

private:
  coroutine_handle_type _coroutine_handle;
};

namespace detail {
template <class T>
inline Task<T> TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{coroutine_handle_type::from_promise(*this)};
}

inline Task<> TaskPromise<void>::get_return_object() noexcept {
  return Task<>{coroutine_handle_type::from_promise(*this)};
}
} // namespace detail
} // namespace libcoro

#endif
