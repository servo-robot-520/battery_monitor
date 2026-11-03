#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>

#include "battery_monitor/bms.hpp"

namespace battery_monitor {

    /// 电池监控节点组件。
    ///
    /// 采用组合而不是继承：内部只持有一个 rclcpp::Node，不继承它。这样其他节点
    /// 可以直接构造本类并传入自己的节点，共享同一个执行器：
    ///
    /// @code
    /// auto node = std::make_shared<rclcpp::Node>("system");
    /// auto monitor = std::make_shared<battery_monitor::BatteryMonitorNode>(node);
    /// @endcode
    ///
    /// 所有参数（bms.chip / bms.i2c_device / publish_rate_hz / frame_id /
    /// location）都声明在传入的节点上。构造失败（设备打不开、参数非法）会抛出
    /// std::runtime_error。
    class BatteryMonitorNode {
    public:
        explicit BatteryMonitorNode(rclcpp::Node::SharedPtr node);

        ~BatteryMonitorNode();

        BatteryMonitorNode(const BatteryMonitorNode&) = delete;
        BatteryMonitorNode& operator=(const BatteryMonitorNode&) = delete;
        BatteryMonitorNode(BatteryMonitorNode&&) = delete;
        BatteryMonitorNode& operator=(BatteryMonitorNode&&) = delete;

        [[nodiscard]] const rclcpp::Node::SharedPtr& node() const noexcept {
            return node_;
        }

        [[nodiscard]] const std::string& bms_name() const noexcept {
            return bms_name_;
        }

    private:
        void poll();

        [[nodiscard]] sensor_msgs::msg::BatteryState to_message(const BatteryReading& reading,
                                                                const rclcpp::Time& stamp) const;

        rclcpp::Node::SharedPtr node_;
        std::unique_ptr<Bms> bms_;
        rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr publisher_;
        rclcpp::TimerBase::SharedPtr timer_;
        std::string bms_name_;
        std::string frame_id_;
        std::string location_;
        std::chrono::steady_clock::time_point last_failure_log_{};
        bool read_failure_active_{false};
    };

} // namespace battery_monitor
