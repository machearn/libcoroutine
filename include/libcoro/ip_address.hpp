#ifndef IP_ADDRESS_HPP
#define IP_ADDRESS_HPP

#include <arpa/inet.h>
#include <array>
#include <span>
#include <string>
#include <sys/socket.h>
namespace libcoro {
namespace socket {
enum class Family : int {
  IPV4 = PF_INET,
  IPV6 = PF_INET6,
};

class IPAddress {
public:
  static const constexpr std::size_t IPV4_SIZE = 4;
  static const constexpr std::size_t IPV6_SIZE = 16;

  IPAddress() noexcept = default;
  IPAddress(std::span<std::uint8_t>& address, Family family): _family(family) {
    if (family == Family::IPV4) {
      if (address.size() != IPV4_SIZE) {
        throw std::invalid_argument("Invalid address size");
      }
    } else {
      if (address.size() != IPV6_SIZE) {
        throw std::invalid_argument("Invalid address size");
      }
    }

    std::copy(address.begin(), address.end(), _address.begin());
  }

  IPAddress(const IPAddress&) noexcept = default;
  IPAddress& operator=(const IPAddress&) noexcept = default;
  IPAddress(IPAddress&&) noexcept = default;
  IPAddress& operator=(IPAddress&&) noexcept = default;
  ~IPAddress() noexcept = default;

  Family family() const noexcept { return _family; }
  std::span<const std::uint8_t> address() const noexcept {
    if (_family == Family::IPV4) {
      return std::span<const std::uint8_t>(_address.begin(), IPV4_SIZE);
    } else {
      return std::span<const std::uint8_t>(_address.begin(), IPV6_SIZE);
    }
  }

  static IPAddress from_string(const std::string& address, Family family) {
    IPAddress ip{};
    ip._family = family;

    auto result = inet_pton(static_cast<int>(family), address.c_str(), ip._address.data());
    if (result == -1) {
      throw std::runtime_error("Failed to convert address");
    }

    return ip;
  }

private:
  Family _family;
  std::array<std::uint8_t, 16> _address;
};
} // namespace socket
} // namespace libcoro

#endif // !IP_ADDRESS_HPP
