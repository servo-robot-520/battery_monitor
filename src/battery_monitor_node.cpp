#include "battery_monitor/battery_monitor_node.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

#include "battery_monitor/bms_factory.hpp"
#include "common_cpp/logging/log_interface/log_macro.hpp"

namespace battery_monitor {
    namespace {

        static_assert(static_cast<uint8_t>(PowerSupplyStatus::UNKNOWN) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_UNKNOWN);
        static_assert(static_cast<uint8_t>(PowerSupplyStatus::CHARGING) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_CHARGING);
        static_assert(static_cast<uint8_t>(PowerSupplyStatus::DISCHARGING) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING);
        static_assert(static_cast<uint8_t>(PowerSupplyStatus::NOT_CHARGING) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_NOT_CHARGING);
        static_assert(static_cast<uint8_t>(PowerSupplyStatus::FULL) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_FULL);

        static_assert(static_cast<uint8_t>(PowerSupplyHealth::UNKNOWN) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_UNKNOWN);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::GOOD) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_GOOD);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::OVERHEAT) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_OVERHEAT);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::DEAD) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_DEAD);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::OVERVOLTAGE) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_OVERVOLTAGE);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::UNSPEC_FAILURE) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_UNSPEC_FAILURE);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::COLD) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_COLD);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::WATCHDOG_TIMER_EXPIRE) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE);
        static_assert(static_cast<uint8_t>(PowerSupplyHealth::SAFETY_TIMER_EXPIRE) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE);

        static_assert(static_cast<uint8_t>(PowerSupplyTechnology::UNKNOWN) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_UNKNOWN);
        static_assert(static_cast<uint8_t>(PowerSupplyTechnology::LION) ==
            sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_LION);

        /// 失败日志最小间隔，避免总线上没有设备时每秒刷屏。
        constexpr auto kFailureLogInterval = std::chrono::seconds(5);

        template <typename T>
        T get_or_declare_parameter(const rclcpp::Node::SharedPtr& node, const std::string& name,
                                   const T& default_value) {
            if (!node->has_parameter(name)) {
                node->declare_parameter<T>(name, default_value);
            }
            return node->get_parameter(name).get_value<T>();
        }

    } // namespace

    BatteryMonitorNode::BatteryMonitorNode(rclcpp::Node::SharedPtr node) :
        node_(std::move(node)) {
        if (!node_) {
            throw std::runtime_error("BatteryMonitorNode requires a non-null rclcpp::Node");
        }

        const auto chip =
            get_or_declare_parameter<std::string>(node_, "bms.chip", "bq28z610");
        const auto i2c_device =
            get_or_declare_parameter<std::string>(node_, "bms.i2c_device", "/dev/i2c-5");
        const auto publish_rate_hz =
            get_or_declare_parameter<double>(node_, "publish_rate_hz", 1.0);
        frame_id_ = get_or_declare_parameter<std::string>(node_, "frame_id", "");
        location_ = get_or_declare_parameter<std::string>(node_, "location", "battery");

        if (!std::isfinite(publish_rate_hz) || publish_rate_hz <= 0.0 ||
            publish_rate_hz > 1000.0) {
            throw std::runtime_error(
                std::format("publish_rate_hz must be in (0, 1000], got {}", publish_rate_hz));
        }

        BmsConfig config;
        config.chip = chip;
        config.i2c_device = i2c_device;

        auto created = create_bms(config);
        if (!created) {
            throw std::runtime_error(created.error().message);
        }
        bms_ = std::move(created.value());
        bms_name_ = bms_->name();

        publisher_ = node_->create_publisher<sensor_msgs::msg::BatteryState>(
            "battery_state", rclcpp::QoS(10));
        timer_ = node_->create_wall_timer(std::chrono::duration<double>(1.0 / publish_rate_hz),
                                          [this] {
                                              poll();
                                          });

        const std::string source = bms_name_ == "fake" ? "simulated" : i2c_device;
        LOG_INFO("Battery monitor ready: chip={} source={} rate={} Hz topic='battery_state'",
                 bms_name_, source, publish_rate_hz);
    }

    BatteryMonitorNode::~BatteryMonitorNode() {
        if (timer_) {
            timer_->cancel();
        }
    }

    void BatteryMonitorNode::poll() {
        auto reading = bms_->read();
        if (!reading) {
            if (const auto now = std::chrono::steady_clock::now(); !read_failure_active_ || now - last_failure_log_ >= kFailureLogInterval) {
                LOG_ERROR("Battery read failed (chip={}): {}", bms_name_, reading.error().message);
                last_failure_log_ = now;
                read_failure_active_ = true;
            }
            return;
        }

        if (read_failure_active_) {
            LOG_INFO("Battery read recovered (chip={})", bms_name_);
            read_failure_active_ = false;
        }

        auto message = to_message(reading.value(), node_->now());
        publisher_->publish(message);
    }

    sensor_msgs::msg::BatteryState BatteryMonitorNode::to_message(
        const BatteryReading& reading, const rclcpp::Time& stamp) const {
        sensor_msgs::msg::BatteryState message;
        message.header.stamp = stamp;
        message.header.frame_id = frame_id_;
        message.voltage = static_cast<float>(reading.voltage);
        message.temperature = static_cast<float>(reading.temperature);
        message.current = static_cast<float>(reading.current);
        message.charge = static_cast<float>(reading.charge);
        message.capacity = static_cast<float>(reading.capacity);
        message.design_capacity = static_cast<float>(reading.design_capacity);
        message.percentage = static_cast<float>(reading.percentage);
        message.power_supply_status = static_cast<uint8_t>(reading.status);
        message.power_supply_health = static_cast<uint8_t>(reading.health);
        message.power_supply_technology = static_cast<uint8_t>(reading.technology);
        message.present = reading.present;
        message.cell_voltage.reserve(reading.cell_voltage.size());
        for (const double cell : reading.cell_voltage) {
            message.cell_voltage.push_back(static_cast<float>(cell));
        }
        message.cell_temperature.reserve(reading.cell_temperature.size());
        for (const double cell : reading.cell_temperature) {
            message.cell_temperature.push_back(static_cast<float>(cell));
        }
        // 可选单体数组由具体 BMS 驱动决定是否填充；不支持的维度保持为空。
        message.location = location_.empty() ? reading.location : location_;
        message.serial_number = reading.serial_number;
        return message;
    }

} // namespace battery_monitor
