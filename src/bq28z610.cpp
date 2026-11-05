#include "battery_monitor/bq28z610.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <thread>
#include <utility>

#include "common_cpp/logging/log_interface/log_macro.hpp"

namespace battery_monitor {
    namespace {

        /// 电流死区：低于该绝对值时视为静置，避免把噪声当作充放电。
        constexpr double kCurrentDeadband = 0.05;

    } // namespace

    Bq28z610::Bq28z610(std::unique_ptr<I2cTransport> transport) :
        transport_(std::move(transport)) {
        if (!transport_) {
            throw std::invalid_argument("Bq28z610 requires an I2C transport");
        }
    }

    std::string Bq28z610::name() const {
        return "bq28z610";
    }

    std::expected<BatteryReading, BmsError> Bq28z610::read() {
        const auto temperature = read_required_word(kCommandTemperature, "temperature");
        if (!temperature) {
            return std::unexpected(temperature.error());
        }

        const auto voltage = read_required_word(kCommandVoltage, "voltage");
        if (!voltage) {
            return std::unexpected(voltage.error());
        }

        const auto status_flags = read_required_word(kCommandBatteryStatus, "battery status");
        if (!status_flags) {
            return std::unexpected(status_flags.error());
        }

        const auto current = read_required_word(kCommandCurrent, "current");
        if (!current) {
            return std::unexpected(current.error());
        }

        const auto remaining_capacity =
            read_required_word(kCommandRemainingCapacity, "remaining capacity");
        if (!remaining_capacity) {
            return std::unexpected(remaining_capacity.error());
        }

        const auto full_charge_capacity =
            read_required_word(kCommandFullChargeCapacity, "full charge capacity");
        if (!full_charge_capacity) {
            return std::unexpected(full_charge_capacity.error());
        }

        const auto state_of_charge =
            read_required_word(kCommandRelativeStateOfCharge, "relative state of charge");
        if (!state_of_charge) {
            return std::unexpected(state_of_charge.error());
        }

        const auto design_capacity =
            read_required_word(kCommandDesignCapacity, "design capacity");
        if (!design_capacity) {
            return std::unexpected(design_capacity.error());
        }

        BatteryReading reading;
        reading.present = true;
        reading.voltage = static_cast<double>(voltage.value()) / 1000.0;
        // TRM/内核 bq27xxx 约定：有符号电流正值为充电。sensor_msgs 同样以正值为充电，
        // 因此这里不需要取反。
        reading.current = static_cast<double>(static_cast<int16_t>(current.value())) / 1000.0;
        reading.charge = static_cast<double>(remaining_capacity.value()) / 1000.0;
        reading.capacity = static_cast<double>(full_charge_capacity.value()) / 1000.0;
        reading.design_capacity = static_cast<double>(design_capacity.value()) / 1000.0;
        reading.percentage =
            static_cast<double>(std::clamp<uint16_t>(state_of_charge.value(), 0, 100)) / 100.0;
        reading.temperature = static_cast<double>(temperature.value()) / 10.0 - 273.15;
        reading.technology = PowerSupplyTechnology::LION;

        if ((status_flags.value() & kFlagFullyCharged) != 0) {
            reading.status = PowerSupplyStatus::FULL;
        }
        else if (reading.current > kCurrentDeadband) {
            reading.status = PowerSupplyStatus::CHARGING;
        }
        else if (reading.current < -kCurrentDeadband) {
            reading.status = PowerSupplyStatus::DISCHARGING;
        }
        else {
            reading.status = PowerSupplyStatus::NOT_CHARGING;
        }

        if ((status_flags.value() & kFlagOvertemperatureAlarm) != 0) {
            reading.health = PowerSupplyHealth::OVERHEAT;
        }
        else if ((status_flags.value() & kFlagOverchargedAlarm) != 0) {
            reading.health = PowerSupplyHealth::OVERVOLTAGE;
        }
        else {
            reading.health = PowerSupplyHealth::GOOD;
        }

        fill_cell_voltages(reading);
        fill_serial_number(reading);
        // BQ28Z610 不提供单体温度，cell_temperature 保持为空；节点按原样映射。
        return reading;
    }

    std::expected<uint16_t, BmsError> Bq28z610::read_required_word(const uint8_t command,
                                                                   const std::string_view field) const {
        const auto value = transport_->read_word(command);
        if (!value) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: failed to read {} (0x{:02X}): {}", field, command,
                            value.error().message)
            });
        }
        return value.value();
    }

    std::expected<std::vector<uint8_t>, BmsError> Bq28z610::read_mac(
        const uint16_t subcommand) const {
        if (const auto written = transport_->write_word(kCommandManufacturerAccess, subcommand); !written) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: failed to issue MAC 0x{:04X}: {}", subcommand,
                            written.error().message)
            });
        }

        // 子命令写入后芯片需要短暂时间准备 MACData。
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        const auto block = transport_->read_bytes(kCommandMacBlockStart, kMacBlockLength);
        if (!block) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: failed to read MAC 0x{:04X} response: {}", subcommand,
                            block.error().message)
            });
        }

        const auto& bytes = block.value();
        if (bytes.size() != kMacBlockLength) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: MAC 0x{:04X} returned {} bytes, expected {}",
                            subcommand, bytes.size(), kMacBlockLength)
            });
        }

        const uint16_t echoed = static_cast<uint16_t>(bytes[0]) |
            static_cast<uint16_t>(bytes[1] << 8);
        if (echoed != subcommand) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: MAC response echo 0x{:04X} does not match request 0x{:04X}",
                            echoed, subcommand)
            });
        }

        // 36 字节块的末两字节固定为校验和与长度；长度包含命令(2) + 数据 + 校验和(1) + 长度(1)。
        const uint8_t reported_length = bytes[kMacBlockLength - 1];
        if (reported_length < 5 || reported_length > kMacBlockLength) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: MAC 0x{:04X} reported invalid length {}", subcommand,
                            reported_length)
            });
        }

        const std::size_t payload_length =
            static_cast<std::size_t>(reported_length) - 4;
        const std::size_t checksummed_length =
            static_cast<std::size_t>(reported_length) - 2;
        uint8_t sum = 0;
        for (std::size_t index = 0; index < checksummed_length; ++index) {
            sum = static_cast<uint8_t>(sum + bytes[index]);
        }
        if (const uint8_t checksum = bytes[kMacBlockLength - 2]; static_cast<uint8_t>(~sum) != checksum) {
            return std::unexpected(BmsError{
                std::format("BQ28Z610: MAC 0x{:04X} checksum mismatch (expected 0x{:02X}, got "
                            "0x{:02X})",
                            subcommand, static_cast<uint8_t>(~sum), checksum)
            });
        }

        return std::vector<uint8_t>(bytes.begin() + 2,
                                    bytes.begin() + 2 + static_cast<std::ptrdiff_t>(payload_length));
    }

    void Bq28z610::fill_cell_voltages(BatteryReading& reading) const {
        const auto response = read_mac(kMacDataStatus1);
        if (!response) {
            // 单体电压是附加信息；MAC 读取失败不影响整包电压/电流/SOC 的发布。
            LOG_DEBUG("{}", response.error().message);
            return;
        }

        const auto& data = response.value();
        if (data.size() < 4) {
            LOG_DEBUG("BQ28Z610: DAStatus1 payload has {} bytes, expected at least 4", data.size());
            return;
        }

        for (const std::size_t offset : {std::size_t{0}, std::size_t{2}}) {
            const auto millivolts = static_cast<uint16_t>(data[offset]) |
                static_cast<uint16_t>(data[offset + 1] << 8);
            if (millivolts > 0) {
                reading.cell_voltage.push_back(static_cast<double>(millivolts) / 1000.0);
            }
        }
    }

    void Bq28z610::fill_serial_number(BatteryReading& reading) const {
        const auto response = read_mac(kMacSerialNumber);
        if (!response) {
            // 序列号是附加信息；读取失败不影响电压/电流/SOC 的发布。
            LOG_DEBUG("{}", response.error().message);
            return;
        }

        const auto& data = response.value();
        if (data.size() < 2) {
            LOG_DEBUG("BQ28Z610: serial number payload has {} bytes, expected at least 2",
                      data.size());
            return;
        }

        const auto serial = static_cast<uint16_t>(data[0]) |
            static_cast<uint16_t>(data[1] << 8);
        reading.serial_number = std::format("0x{:04X}", serial);
    }

} // namespace battery_monitor
