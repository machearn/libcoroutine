#include "libcoro/event_fd.hpp"
#include <stdexcept>
#include <unistd.h>

namespace libcoro {
namespace detail {
#ifdef __APPLE__
EventFD::EventFD() {
  int fds[2];
  if (::pipe(fds) == -1) {
    throw std::runtime_error("Failed to create pipe");
  }
  read_fd = fds[0];
  write_fd = fds[1];
}

void EventFD::close() {
  if (read_fd != -1) {
    ::close(read_fd);
  }
  if (write_fd != -1) {
    ::close(write_fd);
  }
}

EventFD::~EventFD() { close(); }

int EventFD::trigger() {
  char buf[1] = {0};
  return ::write(write_fd, buf, 1);
}

int EventFD::reset() {
  char buf[128];
  return ::read(read_fd, buf, 128);
}
#elif __linux__
// clang-format off
EventFD::EventFD() {
  event_fd = ::eventfd(0, EFD_NONBLOCK);
  if (event_fd == -1) {
    throw std::runtime_error("Failed to create eventfd");
  }
}

EventFD::~EventFD() {
  close();
}

void EventFD::close() {
  if (event_fd != -1) {
    ::close(event_fd);
    event_fd = -1;
  }
}

int EventFD::trigger() {
  uint64_t val = 1;
  return ::write(event_fd, &val, sizeof(val));
}

int EventFD::reset() {
  uint64_t val;
  return ::read(event_fd, &val, sizeof(val));
}
// clang-format on
#endif

} // namespace detail
} // namespace libcoro
