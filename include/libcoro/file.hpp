#ifndef FILE_HPP
#define FILE_HPP

#include "libcoro/io_service.hpp"
#include "libcoro/task.hpp"
#include <cstdio>
#include <memory>
#include <span>

namespace libcoro {
template <concepts::executor Executor>
class File {
  using io_service_ptr = std::shared_ptr<IOService<Executor>>;

public:
  File(io_service_ptr& io_services, std::FILE* fd) noexcept: _fd(fd), _io_service(io_services) {}

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  File(File&& other) noexcept = default;
  File& operator=(File&& other) noexcept = default;

  ~File() { close(); }

  Task<std::span<char>> read(std::size_t size, ::off_t offset = 0);
  Task<std::size_t> write(std::span<const char> data);

  void close();

private:
  std::FILE* _fd;
  io_service_ptr _io_service;
};
} // namespace libcoro

namespace libcoro {
template <concepts::executor Executor>
inline File<Executor> open(std::shared_ptr<IOService<Executor>> io_service, const char* path,
                           const char* mode) {
  auto fd = std::fopen(path, mode);
  return File(io_service, fd);
}

template <concepts::executor Executor>
void File<Executor>::close() {
  if (_fd != nullptr) {
    std::fclose(_fd);
  }
}

template <concepts::executor Executor>
Task<std::span<char>> File<Executor>::read(std::size_t size, ::off_t offset) {
  if (_fd == nullptr) {
    throw std::runtime_error("File descriptor is null");
  }

  co_await _io_service->schedule();

  auto bytes = std::malloc(size);
  auto bytes_size = std::fread(bytes, sizeof(char), size, _fd);

  co_return std::span<char>(static_cast<char*>(bytes), bytes_size);
}

template <concepts::executor Executor>
Task<std::size_t> File<Executor>::write(std::span<const char> data) {
  if (_fd == nullptr) {
    throw std::runtime_error("File descriptor is null");
  }

  co_await _io_service->schedule();

  co_return std::fwrite(data.data(), sizeof(char), data.size(), _fd);
}
} // namespace libcoro

#endif // !FILE_HPP
