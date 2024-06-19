#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#include "concepts/awaitable.hpp"
#include <concepts>
#include <coroutine>

namespace libcoro {
namespace concepts {
template <typename type>
concept executor = requires(type a, std::coroutine_handle<> handle) {
  { a.start() } -> awaiter;
  { a.resume(handle) } -> std::same_as<void>;
};
}
} // namespace libcoro

#endif // !EXECUTOR_HPP
