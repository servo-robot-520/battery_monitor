#include "battery_monitor/bms_factory.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include "battery_monitor/bq28z610.hpp"
#include "battery_monitor/fake_bms.hpp"
#include "battery_monitor/i2c_transport.hpp"

namespace battery_monitor {
    namespace {

        /// 7 位地址中 0x00-0x02 与 0x78-0x7F 为保留地址。
        constexpr uint8_t kMinI2cAddress = 0x03;
        constexpr uint8_t kMaxI2cAddress = 0x77;

        std::string normalize_chip(std::string chip) {
            const auto first = chip.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return {};
            }
            const auto last = chip.find_last_not_of(" \t\r\n");
            chip = chip.substr(first, last - first + 1);
            std::ranges::transform(chip, chip.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return chip;
        }

    } // namespace

    std::expected<std::unique_ptr<Bms>, BmsError> create_bms(const BmsConfig& config) {
        const std::string chip = normalize_chip(config.chip);

        if (chip == "fake") {
            return std::unique_ptr<Bms>(std::make_unique<FakeBms>());
        }

        if (chip == "bq28z610") {
            if (config.i2c_device.empty()) {
                return std::unexpected(BmsError{"bms.i2c_device must not be empty"});
            }
            if (config.i2c_address < kMinI2cAddress || config.i2c_address > kMaxI2cAddress) {
                return std::unexpected(BmsError{
                    std::format("bms.i2c_address 0x{:02X} is not a usable 7-bit address "
                                "(expected 0x{:02X}..0x{:02X})",
                                config.i2c_address, kMinI2cAddress, kMaxI2cAddress)
                });
            }

            auto transport =
                std::make_unique<LinuxI2cTransport>(config.i2c_device, config.i2c_address);
            if (const auto opened = transport->open(); !opened) {
                return std::unexpected(BmsError{opened.error().message});
            }
            return std::unique_ptr<Bms>(std::make_unique<Bq28z610>(std::move(transport)));
        }

        return std::unexpected(BmsError{
            std::format("unsupported bms chip '{}'; supported chips: bq28z610, fake", chip)
        });
    }

} // namespace battery_monitor
