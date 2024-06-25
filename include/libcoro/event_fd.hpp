#ifndef EVENT_FD_HPP
#define EVENT_FD_HPP

namespace libcoro {
namespace detail {
#ifdef __APPLE__
// use pipe on unix to simulate eventfd
struct EventFD {
  EventFD();
  ~EventFD();

  int trigger();
  int reset();

  void close();

  int read_fd{-1};
  int write_fd{-1};
};
#elif __linux__
// clang-format off
struct EventFD {
  EventFD();
  ~EventFD();
  int trigger();
  int reset();
  void close();
  int event_fd{-1};
};
// clang-format on
#endif
} // namespace detail
} // namespace libcoro

#endif // !EVENT_FD_HPP
