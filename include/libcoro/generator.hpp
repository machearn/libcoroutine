#ifndef GENERATOR_HPP
#define GENERATOR_HPP

#include <coroutine>
#include <exception>
#include <memory>
#include <ranges>
#include <type_traits>

namespace libcoro {
template <typename T>
class Generator;

namespace detail {
template <typename T>
class GeneratorPromise {
public:
  using value_type = std::remove_reference_t<T>;
  using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
  using pointer_type = value_type*;

  GeneratorPromise() = default;

  Generator<T> get_return_object() noexcept;

  std::suspend_always initial_suspend() noexcept { return {}; }
  std::suspend_always final_suspend() noexcept { return {}; }

  template <typename U = T, std::enable_if_t<!std::is_rvalue_reference_v<U>, int> = 0>
  std::suspend_always yield_value(std::remove_reference_t<T>& value) noexcept {
    _value = std::addressof(value);
    return {};
  }

  std::suspend_always yield_value(std::remove_reference_t<T>&& value) noexcept {
    _value = std::addressof(value);
    return {};
  }

  void unhandled_exception() { _exception = std::current_exception(); }

  void return_void() noexcept {}

  reference_type value() const noexcept { return static_cast<reference_type>(*_value); }

  template <typename U>
  std::suspend_always await_transform(U&& value) noexcept = delete;

  void rethrow_exception() {
    if (_exception) {
      std::rethrow_exception(_exception);
    }
  }

private:
  pointer_type _value;
  std::exception_ptr _exception;
};

template <typename T>
class GeneratorIterator {
  using coroutine_handle_type = std::coroutine_handle<GeneratorPromise<T>>;

public:
  using iterator_category = std::input_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = typename GeneratorPromise<T>::value_type;
  using reference = typename GeneratorPromise<T>::reference_type;
  using pointer = typename GeneratorPromise<T>::pointer_type;

  GeneratorIterator() noexcept = default;
  explicit GeneratorIterator(coroutine_handle_type coroutine) noexcept: _coroutine(coroutine) {}

  friend bool operator==(const GeneratorIterator& it, std::default_sentinel_t) noexcept {
    return !it._coroutine || it._coroutine.done();
  }

  friend bool operator!=(const GeneratorIterator& it, std::default_sentinel_t) noexcept {
    return !(it == std::default_sentinel);
  }

  friend bool operator==(std::default_sentinel_t, const GeneratorIterator& it) noexcept {
    return it == std::default_sentinel;
  }

  friend bool operator!=(std::default_sentinel_t, const GeneratorIterator& it) noexcept {
    return it != std::default_sentinel;
  }

  GeneratorIterator& operator++() {
    _coroutine.resume();
    if (_coroutine.done()) {
      _coroutine.promise().rethrow_exception();
    }
    return *this;
  }

  GeneratorIterator operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }

  reference operator*() const noexcept { return _coroutine.promise().value(); }
  pointer operator->() const noexcept { return std::addressof(operator*()); }

private:
  coroutine_handle_type _coroutine = nullptr;
};
} // namespace detail

template <typename T>
class Generator: public std::ranges::view_base {
public:
  using promise_type = detail::GeneratorPromise<T>;
  using iterator = detail::GeneratorIterator<T>;

  Generator() noexcept: _coroutine(nullptr) {}

  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  Generator(Generator&& other) noexcept: _coroutine(other._coroutine) {
    other._coroutine = nullptr;
  }

  Generator& operator=(Generator&& other) noexcept {
    if (this != &other) {
      if (_coroutine) {
        _coroutine.destroy();
      }
      _coroutine = other._coroutine;
      other._coroutine = nullptr;
    }
    return *this;
  }

  ~Generator() {
    if (_coroutine) {
      _coroutine.destroy();
    }
  }

  iterator begin() {
    if (_coroutine) {
      _coroutine.resume();
      if (_coroutine.done()) {
        _coroutine.promise().rethrow_exception();
      }
    }
    return iterator{_coroutine};
  }

  std::default_sentinel_t end() noexcept { return std::default_sentinel; }

private:
  friend class detail::GeneratorPromise<T>;
  explicit Generator(std::coroutine_handle<promise_type> coroutine) noexcept
      : _coroutine(coroutine) {}
  std::coroutine_handle<promise_type> _coroutine;
};

namespace detail {
template <typename T>
Generator<T> GeneratorPromise<T>::get_return_object() noexcept {
  return Generator<T>{std::coroutine_handle<GeneratorPromise>::from_promise(*this)};
}
} // namespace detail

} // namespace libcoro

#endif // !GENERATOR_HPP
