#ifndef AWAITABLE_HPP
#define AWAITABLE_HPP

#include <coroutine>

namespace libcoro {
namespace concepts {

template <typename type, typename... types>
concept in_types = (std::same_as<type, types> || ...);

template <typename type>
concept awaiter = requires(type a) {
  { a.await_ready() } -> std::same_as<bool>;
  { a.await_suspend() } -> in_types<void, bool, std::coroutine_handle<>>;
  { a.await_resume() };
};

template <typename type>
concept co_awaitable_member = requires(type a) {
  { a.operator co_await() } -> awaiter;
};

template <typename type>
concept co_awaitable_global = requires(type a) {
  { operator co_await(a) } -> awaiter;
};

template <typename type>
concept awaitable = co_awaitable_member<type> || co_awaitable_global<type> || awaiter<type>;

template <typename type>
concept awaiter_void = awaiter<type> && requires(type a) {
  { a.await_resume() } -> std::same_as<void>;
};

template <typename type>
concept co_awaitable_member_void = requires(type a) {
  { a.operator co_await() } -> awaiter_void;
};

template <typename type>
concept co_awaitable_global_void = requires(type a) {
  { operator co_await(a) } -> awaiter_void;
};

template <typename type>
concept awaitable_void =
    co_awaitable_member_void<type> || co_awaitable_global_void<type> || awaiter_void<type>;

template <awaitable T, typename = void>
struct awaitable_traits {};

template <awaitable T>
static auto get_awaiter(T&& a) {
  if constexpr (co_awaitable_member<T>) {
    return std::forward<T>(a).operator co_await();
  } else if constexpr (co_awaitable_global<T>) {
    return operator co_await(std::forward<T>(a));
  } else {
    return std::forward<T>(a);
  }
}

template <awaitable T>
struct awaitable_traits<T> {
  using awaiter_t = decltype(get_awaiter(std::declval<T>()));
  using awaiter_return_t = decltype(std::declval<awaiter_t>().await_resume());
};

} // namespace concepts
} // namespace libcoro

#endif // !AWAITABLE_HPP
