#include "libcoro/event_fd.hpp"
#include <stdexcept>
#include <unistd.h>

namespace libcoro {
namespace detail {

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

int EventFD::trigger() { return ::write(write_fd, "a", 1); }
int EventFD::reset() {
  char buf[128];
  return ::read(read_fd, buf, 128);
}

} // namespace detail
} // namespace libcoro
