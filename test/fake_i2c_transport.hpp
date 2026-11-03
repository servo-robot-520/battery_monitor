#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "battery_monitor/i2c_transport.hpp"

namespace battery_monitor::test {

    /// 内存中的 BQ28Z610 从机，用于在没有硬件时验证寄存器解码。
    class FakeI2cTransport final : public I2cTransport {
    public:
        /// 命令 -> 寄存器值（SMBus 字）。
        std::map<uint8_t, uint16_t> registers;
        /// 需要模拟读失败的命令。
        std::set<uint8_t> failing_reads;
        /// MAC 子命令 -> MACData 负载（不含命令回显、校验和与长度）。
        std::map<uint16_t, std::vector<uint8_t>> mac_payloads;
        bool fail_mac_read{false};
        /// 记录驱动发出的所有字写操作。
        std::vector<std::pair<uint8_t, uint16_t>> writes;

        std::expected<uint16_t, I2cError> read_word(const uint8_t command) override {
            if (failing_reads.contains(command)) {
                return std::unexpected(I2cError{
                    5, std::format("fake read error for command 0x{:02X}", command)
                });
            }
            const auto register_it = registers.find(command);
            if (register_it == registers.end()) {
                return std::unexpected(I2cError{
                    5, std::format("fake register 0x{:02X} is not mapped", command)
                });
            }
            return register_it->second;
        }

        std::expected<void, I2cError> write_word(const uint8_t command,
                                                  const uint16_t value) override {
            writes.emplace_back(command, value);
            if (command == 0x00) {
                pending_mac_ = value;
            }
            return {};
        }

        std::expected<std::vector<uint8_t>, I2cError> read_bytes(
            const uint8_t command, const std::size_t length) override {
            if (command != 0x3E) {
                return std::unexpected(I2cError{
                    5, std::format("fake byte read only supports 0x3E, got 0x{:02X}", command)
                });
            }
            if (fail_mac_read) {
                return std::unexpected(I2cError{5, "fake MAC read failure"});
            }
            if (length != 36) {
                return std::unexpected(I2cError{
                    5, std::format("fake MAC block length must be 36, got {}", length)
                });
            }
            if (!pending_mac_.has_value()) {
                return std::unexpected(I2cError{5, "fake MAC read without a subcommand"});
            }

            const auto payload_it = mac_payloads.find(pending_mac_.value());
            if (payload_it == mac_payloads.end()) {
                return std::unexpected(I2cError{
                    5, std::format("fake has no MAC payload for 0x{:04X}", pending_mac_.value())
                });
            }

            const auto& payload = payload_it->second;
            if (payload.size() > 32) {
                return std::unexpected(I2cError{5, "fake MAC payload exceeds 32 bytes"});
            }

            std::vector<uint8_t> block(36, 0);
            block[0] = static_cast<uint8_t>(pending_mac_.value() & 0xFF);
            block[1] = static_cast<uint8_t>(pending_mac_.value() >> 8);
            for (std::size_t index = 0; index < payload.size(); ++index) {
                block[2 + index] = payload[index];
            }

            const auto reported_length = static_cast<uint8_t>(payload.size() + 4);
            const auto checksummed_length = static_cast<std::size_t>(reported_length) - 2;
            uint8_t sum = 0;
            for (std::size_t index = 0; index < checksummed_length; ++index) {
                sum = static_cast<uint8_t>(sum + block[index]);
            }
            block[34] = static_cast<uint8_t>(~sum);
            block[35] = reported_length;
            return block;
        }

        /// 构造 DAStatus1 的 32 字节数据区（只有前 4 字节是单体电压）。
        static std::vector<uint8_t> make_dastatus1_payload(const uint16_t cell1_mv,
                                                            const uint16_t cell2_mv) {
            std::vector<uint8_t> payload(32, 0);
            payload[0] = static_cast<uint8_t>(cell1_mv & 0xFF);
            payload[1] = static_cast<uint8_t>(cell1_mv >> 8);
            payload[2] = static_cast<uint8_t>(cell2_mv & 0xFF);
            payload[3] = static_cast<uint8_t>(cell2_mv >> 8);
            return payload;
        }

    private:
        std::optional<uint16_t> pending_mac_;
    };

} // namespace battery_monitor::test
