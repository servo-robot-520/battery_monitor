#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace battery_monitor {

    /// 一次 I2C 传输失败的原因。code 保留 errno，便于上层区分总线/从机错误。
    struct I2cError {
        int code{0};
        std::string message;
    };

    /// BMS 驱动依赖的最小 I2C 操作集合。
    ///
    /// 驱动只关心 SMBus 字读写和原始字节块读取，因此测试可以注入一个内存中的
    /// 假从机（见 test/fake_i2c_transport.hpp），无需真实 /dev/i2c-*。
    class I2cTransport {
    public:
        virtual ~I2cTransport() = default;

        /// 读取 SMBus 字（命令后两个字节，低字节在前）。
        virtual std::expected<uint16_t, I2cError> read_word(uint8_t command) = 0;

        /// 写入 SMBus 字（命令后两个字节，低字节在前）。
        virtual std::expected<void, I2cError> write_word(uint8_t command, uint16_t value) = 0;

        /// 以 command 为起始地址读取 length 个原始字节。
        ///
        /// 用于 BQ28Z610 的 MAC 响应：芯片要求从 0x3E 起连续读取 36 字节，
        /// 超出 SMBus 块读取 32 字节上限，因此必须走原始 I2C 传输。
        virtual std::expected<std::vector<uint8_t>, I2cError> read_bytes(
            uint8_t command, std::size_t length) = 0;
    };

    /// Linux i2c-dev 实现：/dev/i2c-N + 7 位从机地址。
    class LinuxI2cTransport final : public I2cTransport {
    public:
        LinuxI2cTransport(std::string device, uint8_t address);

        LinuxI2cTransport(const LinuxI2cTransport&) = delete;
        LinuxI2cTransport& operator=(const LinuxI2cTransport&) = delete;

        ~LinuxI2cTransport() override;

        /// 打开设备并把从机地址绑定到文件描述符。重复调用是幂等的。
        std::expected<void, I2cError> open();

        std::expected<uint16_t, I2cError> read_word(uint8_t command) override;
        std::expected<void, I2cError> write_word(uint8_t command, uint16_t value) override;
        std::expected<std::vector<uint8_t>, I2cError> read_bytes(
            uint8_t command, std::size_t length) override;

    private:
        std::string device_;
        uint8_t address_;
        int fd_{-1};
    };

} // namespace battery_monitor
