#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bms.hpp"
#include "i2c_transport.hpp"

namespace battery_monitor {

    /// TI BQ28Z610 电量计驱动。
    ///
    /// 寄存器地址来自 docs/BQ28Z620-R1TechnicalReferenceManual.pdf 的
    /// Table 14-1。注意该芯片并不使用 SBS 标准地址（例如电压是 0x08 而不是
    /// 0x09，SOC 是 0x2C 而不是 0x0D）；docs/BQ28Z610-寄存器手册.md 中列出的
    /// 是通用 SBS 地址，不能直接用于该芯片。
    ///
    /// 该芯片只提供整包温度，不提供单体温度，因此 BatteryReading 的
    /// cell_temperature 保持为空。
    class Bq28z610 final : public Bms {
    public:
        explicit Bq28z610(std::unique_ptr<I2cTransport> transport);

        [[nodiscard]] std::string name() const override;

        std::expected<BatteryReading, BmsError> read() override;

    private:
        static constexpr uint8_t kCommandTemperature = 0x06;
        static constexpr uint8_t kCommandVoltage = 0x08;
        static constexpr uint8_t kCommandBatteryStatus = 0x0A;
        static constexpr uint8_t kCommandCurrent = 0x0C;
        static constexpr uint8_t kCommandRemainingCapacity = 0x10;
        static constexpr uint8_t kCommandFullChargeCapacity = 0x12;
        static constexpr uint8_t kCommandRelativeStateOfCharge = 0x2C;
        static constexpr uint8_t kCommandDesignCapacity = 0x3C;
        static constexpr uint8_t kCommandManufacturerAccess = 0x00;
        static constexpr uint8_t kCommandMacBlockStart = 0x3E;
        static constexpr std::size_t kMacBlockLength = 36;
        static constexpr uint16_t kMacDataStatus1 = 0x0071;

        /// BatteryStatus() 位定义，见 TRM 14.1.6。
        static constexpr uint16_t kFlagFullyCharged = 1U << 5;
        static constexpr uint16_t kFlagOvertemperatureAlarm = 1U << 12;
        static constexpr uint16_t kFlagOverchargedAlarm = 1U << 15;

        [[nodiscard]] std::expected<uint16_t, BmsError> read_required_word(uint8_t command,
                                                             std::string_view field) const;

        /// 读取一次 MAC 响应。BQ28Z610 要求从 0x3E 起连续读取 36 字节，
        /// 响应首两字节是子命令回显，末两字节是校验和与长度。
        [[nodiscard]] std::expected<std::vector<uint8_t>, BmsError> read_mac(uint16_t subcommand) const;

        /// 从 DAStatus1 补齐单体电压；失败只影响单体电压，不影响整包数据。
        void fill_cell_voltages(BatteryReading& reading) const;

        std::unique_ptr<I2cTransport> transport_;
    };

} // namespace battery_monitor
