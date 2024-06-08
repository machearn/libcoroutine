#ifndef FILE_HPP
#define FILE_HPP

#include "libcoro/io_service.hpp"
#include "libcoro/task.hpp"
#include <cstdio>
#include <memory>
#include <span>

namespace libcoro {

class File {
  using io_service_ptr = std::shared_ptr<IOService>;

public:
  File(io_service_ptr& io_services, std::FILE* fd) noexcept: _fd(fd), _io_service(io_services) {}

  File(const File&) = delete;
  File& operator=(const File&) = delete;

  File(File&& other) noexcept = default;
  File& operator=(File&& other) noexcept = default;

  ~File() { close(); }

  static File open(io_service_ptr, const char*, const char*);

  Task<std::span<char>> read(std::size_t size, ::off_t offset = 0);
  Task<std::size_t> write(std::span<const char> data);

  void close();

private:
  std::FILE* _fd;
  io_service_ptr _io_service;
};
} // namespace libcoro

#endif // !FILE_HPP
