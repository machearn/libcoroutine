#ifndef EVENT_FD_HPP
#define EVENT_FD_HPP

namespace libcoro {
namespace detail {
// use pipe on unix to simulate eventfd
struct EventFD {
  EventFD();
  ~EventFD();

  int trigger();
  int reset();

  void close();

  int read_fd;
  int write_fd;
};
} // namespace detail
} // namespace libcoro

#endif // !EVENT_FD_HPP
