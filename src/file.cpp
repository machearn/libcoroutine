#include "libcoro/file.hpp"

namespace libcoro {
File File::open(File::io_service_ptr io_service, const char* path, const char* mode) {
  auto fd = std::fopen(path, mode);
  return File(io_service, fd);
}

void File::close() {
  if (_fd != nullptr) {
    std::fclose(_fd);
  }
}

Task<std::span<char>> File::read(std::size_t size, ::off_t offset) {
  if (_fd == nullptr) {
    throw std::runtime_error("File descriptor is null");
  }

  co_await _io_service->await();

  auto bytes = std::malloc(size);
  auto bytes_size = std::fread(bytes, sizeof(char), size, _fd);

  co_return std::span<char>(static_cast<char*>(bytes), bytes_size);
}

Task<std::size_t> File::write(std::span<const char> data) {
  if (_fd == nullptr) {
    throw std::runtime_error("File descriptor is null");
  }

  co_await _io_service->await();

  co_return std::fwrite(data.data(), sizeof(char), data.size(), _fd);
}
} // namespace libcoro
