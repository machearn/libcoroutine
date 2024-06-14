#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#include "concepts/awaitable.hpp"

namespace libcoro {
namespace concepts {
template <typename type>
concept executor = requires(type a) {
  { a.execute() } -> awaiter;
};
}
} // namespace libcoro

#endif // !EXECUTOR_HPP
