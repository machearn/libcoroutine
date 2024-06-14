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

} // namespace concepts
} // namespace libcoro

#endif // !AWAITABLE_HPP
