#ifndef SOCKET_HPP
#define SOCKET_HPP

#include "concepts/executor.hpp"
#include "libcoro/io_service.hpp"
#include "libcoro/ip_address.hpp"
#include "libcoro/poll.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

namespace libcoro {
namespace socket {
enum class TransferStatus : std::int64_t {
  OK = 0,
  CLOSED = -1,
  TRY_AGAIN = EAGAIN,
  WOULD_BLOCK = EWOULDBLOCK,
  BAD_FILE_DESCRIPTOR = EBADF,
  CONNECTION_REFUSED = ECONNREFUSED,
  MEMORY_FAULT = EFAULT,
  INTERRUPTED = EINTR,
  INVALID_ARGUMENT = EINVAL,
  NO_MEMORY = ENOMEM,
  NOT_CONNECTED = ENOTCONN,
  NOT_SOCKET = ENOTSOCK,
  PERMISSION_DENIED = EACCES,
  ALREADY_IN_PROGRESS = EALREADY,
  CONNECTION_RESET = ECONNRESET,
  NO_PEER_ADDRESS = EDESTADDRREQ,
  IS_CONNECTION = EISCONN,
  MESSAGE_SIZE = EMSGSIZE,
  OUTPUT_QUEUE_FULL = ENOBUFS,
  OPERATION_NOT_SUPPORTED = EOPNOTSUPP,
  PIPE_ERROR = EPIPE,
};
enum class ConnectStatus : std::int64_t {
  CONENCTED,
  INVALID_ADDRESS,
  TIMEOUT,
  ERROR,
};
enum class Protocol : int {
  TCP = SOCK_STREAM,
  UDP = SOCK_DGRAM,
};
} // namespace socket

template <concepts::executor Executor>
class Socket {
  using io_service_ptr = std::shared_ptr<IOService<Executor>>;

public:
  Socket(io_service_ptr& io_services, int fd) noexcept: _fd(fd), _io_service(io_services) {}

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) noexcept = default;
  Socket& operator=(Socket&& other) noexcept = default;

  Task<detail::PollStatus> poll();
  Task<detail::PollStatus> poll(detail::PollType);

  Task<socket::ConnectStatus> connect(const socket::IPAddress& addr, int port);
  int bind(int port, const socket::IPAddress& address);

  Socket accept();
  template <concepts::executor T>
  Socket<T> accept(std::shared_ptr<IOService<T>> io_service) {
    struct sockaddr_in client_addr;
    constexpr const auto client_addr_len = sizeof(client_addr);

    auto fd =
        ::accept(_fd, reinterpret_cast<struct sockaddr*>(&client_addr),
                 const_cast<socklen_t*>(reinterpret_cast<const socklen_t*>(&client_addr_len)));

    if (fd == -1) {
      throw std::runtime_error("Failed to accept connection");
    }

    return Socket<T>(io_service, fd);
  }

  Task<std::pair<socket::TransferStatus, std::span<char>>> recieve(std::size_t size);
  std::pair<socket::TransferStatus, std::span<char>> recieve_sync(std::size_t size);

  Task<std::pair<socket::TransferStatus, std::size_t>> send(std::span<const char> data);
  std::pair<socket::TransferStatus, std::size_t> send_sync(std::span<const char> data);

  void close();
  bool shutdown(detail::PollType how);

private:
  int _fd;
  io_service_ptr _io_service;

  std::optional<socket::ConnectStatus> _connect_status{std::nullopt};
};
} // namespace libcoro

namespace libcoro {
// tempararily support only TCP IPv4
template <concepts::executor Executor>
inline Socket<Executor> create_socket(std::shared_ptr<IOService<Executor>>& io_service,
                                      socket::Family family, socket::Protocol protocol) {
  auto fd = ::socket(static_cast<int>(family), static_cast<int>(protocol), 0);
  auto flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    throw std::runtime_error("Failed to get file flags");
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    throw std::runtime_error("Failed to set file flags");
  }
  return Socket(io_service, fd);
}

template <concepts::executor Executor>
Task<detail::PollStatus> Socket<Executor>::poll() {
  return _io_service->poll(_fd, detail::PollType::READ);
}

template <concepts::executor Executor>
Task<detail::PollStatus> Socket<Executor>::poll(detail::PollType poll_type) {
  return _io_service->poll(_fd, poll_type);
}

template <concepts::executor Executor>
Task<socket::ConnectStatus> Socket<Executor>::connect(const socket::IPAddress& addr, int port) {
  if (_connect_status.has_value()) {
    co_return _connect_status.value();
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = static_cast<int>(addr.family());
  server_addr.sin_port = htons(port);
  server_addr.sin_addr = *reinterpret_cast<const struct in_addr*>(addr.address().data());

  auto ret = ::connect(_fd, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));
  if (ret == 0) {
    _connect_status = socket::ConnectStatus::CONENCTED;
    co_return socket::ConnectStatus::CONENCTED;
  } else if (ret == -1) {
    if (errno == EINPROGRESS) {
      auto poll_status = co_await _io_service->poll(_fd, detail::PollType::WRITE);
      if (poll_status == detail::PollStatus::EVENT_READY) {
        int result = 0;
        socklen_t result_len = sizeof(result);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &result, &result_len) == -1) {
          throw std::runtime_error("Failed to get socket options");
        }
        if (result == 0) {
          _connect_status = socket::ConnectStatus::CONENCTED;
          co_return socket::ConnectStatus::CONENCTED;
        }
        // TODO: poll timeout not supported yet
      } else if (poll_status == detail::PollStatus::EVENT_TIMEOUT) {
        _connect_status = socket::ConnectStatus::TIMEOUT;
        co_return socket::ConnectStatus::TIMEOUT;
      }
    }
  }

  _connect_status = socket::ConnectStatus::ERROR;
  co_return socket::ConnectStatus::ERROR;
}

template <concepts::executor Executor>
auto Socket<Executor>::accept() -> Socket {
  return accept(_io_service);
}

template <concepts::executor Executor>
Task<std::pair<socket::TransferStatus, std::span<char>>>
Socket<Executor>::recieve(std::size_t size) {
  if (_fd == -1) {
    throw std::runtime_error("File descriptor is null");
  }

  co_await _io_service->schedule();

  auto buffer = std::malloc(size);
  auto bytes = ::recv(_fd, buffer, size, 0);

  if (bytes > 0) {
    co_return {socket::TransferStatus::OK, std::span<char>(static_cast<char*>(buffer), bytes)};
  } else if (bytes == 0) {
    co_return {socket::TransferStatus::CLOSED, std::span<char>()};
  } else {
    co_return {static_cast<socket::TransferStatus>(errno), std::span<char>()};
  }
}

template <concepts::executor Executor>
std::pair<socket::TransferStatus, std::span<char>>
Socket<Executor>::recieve_sync(std::size_t size) {
  auto buffer = std::malloc(size);
  auto bytes = ::recv(_fd, buffer, size, 0);
  if (bytes > 0) {
    return {socket::TransferStatus::OK, std::span<char>(static_cast<char*>(buffer), bytes)};
  } else if (bytes == 0) {
    return {socket::TransferStatus::CLOSED, {}};
  } else {
    return {static_cast<socket::TransferStatus>(errno), {}};
  }
}

template <concepts::executor Executor>
Task<std::pair<socket::TransferStatus, std::size_t>>
Socket<Executor>::send(std::span<const char> data) {
  if (_fd == -1) {
    throw std::runtime_error("File descriptor is null");
  }
  co_await _io_service->schedule();
  auto bytes = ::send(_fd, data.data(), data.size(), 0);
  if (bytes >= 0) {
    co_return {socket::TransferStatus::OK, bytes};
  } else {
    co_return {static_cast<socket::TransferStatus>(errno), 0};
  }
}

template <concepts::executor Executor>
std::pair<socket::TransferStatus, std::size_t>
Socket<Executor>::send_sync(std::span<const char> data) {
  auto bytes = ::send(_fd, data.data(), data.size(), 0);
  if (bytes >= 0) {
    return {socket::TransferStatus::OK, bytes};
  } else {
    return {static_cast<socket::TransferStatus>(errno), 0};
  }
}

template <concepts::executor Executor>
void Socket<Executor>::close() {
  if (_fd != -1) {
    ::close(_fd);
    _fd = -1;
  }
}

template <concepts::executor Executor>
bool Socket<Executor>::shutdown(detail::PollType how) {
  if (_fd != -1) {
    int h = 0;
    switch (how) {
    case detail::PollType::READ:
      h = SHUT_RD;
      break;
    case detail::PollType::WRITE:
      h = SHUT_WR;
      break;
#ifdef __linux__
    case detail::PollType::READ_WRITE:
      h = SHUT_RDWR;
      break;
#endif
    }

    return ::shutdown(_fd, h) == 0;
  }

  return false;
}
} // namespace libcoro

#endif // !SOCKET_HPP
