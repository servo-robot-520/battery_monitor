#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace battery_monitor {

    /// 与 sensor_msgs/msg/BatteryState 的常量逐值对应，节点直接做静态转换。
    enum class PowerSupplyStatus : uint8_t {
        UNKNOWN = 0,
        CHARGING = 1,
        DISCHARGING = 2,
        NOT_CHARGING = 3,
        FULL = 4,
    };

    enum class PowerSupplyHealth : uint8_t {
        UNKNOWN = 0,
        GOOD = 1,
        OVERHEAT = 2,
        DEAD = 3,
        OVERVOLTAGE = 4,
        UNSPEC_FAILURE = 5,
        COLD = 6,
        WATCHDOG_TIMER_EXPIRE = 7,
        SAFETY_TIMER_EXPIRE = 8,
    };

    enum class PowerSupplyTechnology : uint8_t {
        UNKNOWN = 0,
        NIMH = 1,
        LION = 2,
        LIPO = 3,
        LIFE = 4,
        NICD = 5,
        LIMN = 6,
        LI24V = 7,
        LI48V = 8,
    };

    /// 一次成功采样后的电池状态，单位已转换为 SI/ROS 约定。
    ///
    /// 电流采用 sensor_msgs/BatteryState 的约定：正值为充电，负值为放电。
    /// 电压、容量、温度分别使用 V、Ah、°C。
    struct BatteryReading {
        bool present{false};
        double voltage{0.0};
        double current{0.0};
        double charge{0.0};
        double capacity{0.0};
        double design_capacity{0.0};
        double percentage{0.0};
        double temperature{0.0};
        std::vector<double> cell_voltage;
        /// 单体温度；具体 BMS 不提供时留空，由节点原样映射到消息。
        std::vector<double> cell_temperature;
        std::string location;
        std::string serial_number;
        PowerSupplyStatus status{PowerSupplyStatus::UNKNOWN};
        PowerSupplyHealth health{PowerSupplyHealth::UNKNOWN};
        PowerSupplyTechnology technology{PowerSupplyTechnology::UNKNOWN};
    };

    struct BmsError {
        std::string message;
    };

    /// 具体 BMS 芯片的抽象。新增芯片只需实现该接口并在 bms_factory 中注册。
    class Bms {
    public:
        virtual ~Bms() = default;

        /// 芯片标识，用于日志与配置回显，例如 "bq28z610"。
        [[nodiscard]] virtual std::string name() const = 0;

        /// 读取一次完整电池状态。任何必需字段读取失败都返回错误，节点不会发布陈旧数据。
        virtual std::expected<BatteryReading, BmsError> read() = 0;
    };

} // namespace battery_monitor
