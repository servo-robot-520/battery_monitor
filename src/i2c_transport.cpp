#include "battery_monitor/i2c_transport.hpp"

#include <cerrno>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace battery_monitor {
    namespace {

        std::string describe_errno(const int code) {
            return std::format("{} (errno {})", std::strerror(code), code);
        }

        /// 新版内核 UAPI 不再提供 i2c_smbus_* 内联封装，直接走 I2C_SMBUS ioctl。
        std::expected<void, I2cError> smbus_transfer(const int fd, const std::string& device,
                                                      const uint8_t address, const uint8_t command,
                                                      const char read_write, const uint8_t size,
                                                      i2c_smbus_data* data) {
            i2c_smbus_ioctl_data transfer = {};
            transfer.read_write = read_write;
            transfer.command = command;
            transfer.size = size;
            transfer.data = data;

            errno = 0;
            if (::ioctl(fd, I2C_SMBUS, &transfer) < 0) {
                const int error = errno;
                return std::unexpected(I2cError{
                    error,
                    std::format("SMBus command 0x{:02X} on 0x{:02X} at {} failed: {}", command,
                                address, device, describe_errno(error))
                });
            }
            return {};
        }

    } // namespace

    LinuxI2cTransport::LinuxI2cTransport(std::string device, const uint8_t address)
        : device_(std::move(device)), address_(address) {}

    LinuxI2cTransport::~LinuxI2cTransport() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    std::expected<void, I2cError> LinuxI2cTransport::open() {
        if (fd_ >= 0) {
            return {};
        }

        const int fd = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            const int error = errno;
            return std::unexpected(I2cError{
                error, std::format("cannot open {}: {}", device_, describe_errno(error))
            });
        }

        if (::ioctl(fd, I2C_SLAVE, static_cast<unsigned long>(address_)) < 0) {
            const int error = errno;
            ::close(fd);
            return std::unexpected(I2cError{
                error,
                std::format("cannot select 0x{:02X} on {}: {}", address_, device_,
                            describe_errno(error))
            });
        }

        fd_ = fd;
        return {};
    }

    std::expected<uint16_t, I2cError> LinuxI2cTransport::read_word(const uint8_t command) {
        if (fd_ < 0) {
            return std::unexpected(I2cError{EBADF, std::format("{} is not open", device_)});
        }

        i2c_smbus_data data = {};
        if (const auto result = smbus_transfer(fd_, device_, address_, command, I2C_SMBUS_READ,
                                                I2C_SMBUS_WORD_DATA, &data);
            !result) {
            return std::unexpected(I2cError{
                result.error().code,
                std::format("read word failed: {}", result.error().message)
            });
        }

        return static_cast<uint16_t>(data.word);
    }

    std::expected<void, I2cError> LinuxI2cTransport::write_word(const uint8_t command,
                                                                const uint16_t value) {
        if (fd_ < 0) {
            return std::unexpected(I2cError{EBADF, std::format("{} is not open", device_)});
        }

        i2c_smbus_data data = {};
        data.word = value;
        if (const auto result = smbus_transfer(fd_, device_, address_, command, I2C_SMBUS_WRITE,
                                                I2C_SMBUS_WORD_DATA, &data);
            !result) {
            return std::unexpected(I2cError{
                result.error().code,
                std::format("write word failed: {}", result.error().message)
            });
        }

        return {};
    }

    std::expected<std::vector<uint8_t>, I2cError> LinuxI2cTransport::read_bytes(
        const uint8_t command, const std::size_t length) {
        if (fd_ < 0) {
            return std::unexpected(I2cError{EBADF, std::format("{} is not open", device_)});
        }
        if (length == 0 || length > std::numeric_limits<uint16_t>::max()) {
            return std::unexpected(I2cError{
                EINVAL, std::format("invalid I2C read length {}", length)
            });
        }

        std::vector<uint8_t> buffer(length);
        uint8_t command_byte = command;

        i2c_msg messages[2] = {};
        messages[0].addr = address_;
        messages[0].flags = 0;
        messages[0].len = 1;
        messages[0].buf = &command_byte;
        messages[1].addr = address_;
        messages[1].flags = I2C_M_RD;
        messages[1].len = static_cast<uint16_t>(length);
        messages[1].buf = buffer.data();

        i2c_rdwr_ioctl_data transfer = {};
        transfer.msgs = messages;
        transfer.nmsgs = 2;

        errno = 0;
        if (::ioctl(fd_, I2C_RDWR, &transfer) < 0) {
            const int error = errno;
            return std::unexpected(I2cError{
                error,
                std::format("read {} bytes from 0x{:02X} on 0x{:02X} at {} failed: {}", length,
                            command, address_, device_, describe_errno(error))
            });
        }

        return buffer;
    }

} // namespace battery_monitor
