#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>

#include "battery_monitor/battery_monitor_node.hpp"

namespace battery_monitor {
    namespace {

        class BatteryMonitorNodeTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                if (!rclcpp::ok()) {
                    rclcpp::init(0, nullptr);
                }
            }

            static void TearDownTestSuite() {
                if (rclcpp::ok()) {
                    rclcpp::shutdown();
                }
            }
        };

        TEST_F(BatteryMonitorNodeTest, PublishesFakeBatteryState) {
            const auto options = rclcpp::NodeOptions().parameter_overrides({
                rclcpp::Parameter("bms.chip", "fake"),
                rclcpp::Parameter("publish_rate_hz", 5.0),
                rclcpp::Parameter("location", "fake_pack"),
            });
            const auto node =
                std::make_shared<rclcpp::Node>("battery_monitor_test", options);
            const auto monitor = std::make_shared<BatteryMonitorNode>(node);

            sensor_msgs::msg::BatteryState received;
            int count = 0;
            const auto subscription =
                node->create_subscription<sensor_msgs::msg::BatteryState>(
                    "battery_state", 10,
                    [&received, &count](const sensor_msgs::msg::BatteryState::ConstSharedPtr message) {
                        received = *message;
                        ++count;
                    });

            rclcpp::executors::SingleThreadedExecutor executor;
            executor.add_node(node);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (count == 0 && std::chrono::steady_clock::now() < deadline) {
                executor.spin_once(std::chrono::milliseconds(50));
            }
            executor.remove_node(node);

            ASSERT_GT(count, 0);
            EXPECT_TRUE(received.present);
            EXPECT_FLOAT_EQ(7.8F, received.voltage);
            EXPECT_FLOAT_EQ(0.0F, received.current);
            EXPECT_FLOAT_EQ(2.0F, received.charge);
            EXPECT_FLOAT_EQ(2.5F, received.capacity);
            EXPECT_FLOAT_EQ(0.8F, received.percentage);
            EXPECT_EQ(sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_NOT_CHARGING,
                      received.power_supply_status);
            EXPECT_EQ(sensor_msgs::msg::BatteryState::POWER_SUPPLY_HEALTH_GOOD,
                      received.power_supply_health);
            EXPECT_EQ(sensor_msgs::msg::BatteryState::POWER_SUPPLY_TECHNOLOGY_LION,
                      received.power_supply_technology);
            ASSERT_EQ(2U, received.cell_voltage.size());
            ASSERT_EQ(2U, received.cell_temperature.size());
            EXPECT_FLOAT_EQ(24.5F, received.cell_temperature[0]);
            EXPECT_EQ("fake_pack", received.location);
            EXPECT_EQ("FAKE-0001", received.serial_number);
            EXPECT_FALSE(received.header.stamp.sec == 0 && received.header.stamp.nanosec == 0);
        }

        TEST_F(BatteryMonitorNodeTest, RejectsUnknownChip) {
            const auto options = rclcpp::NodeOptions().parameter_overrides({
                rclcpp::Parameter("bms.chip", "missing-chip"),
            });
            const auto node =
                std::make_shared<rclcpp::Node>("battery_monitor_bad_chip", options);

            EXPECT_THROW(BatteryMonitorNode monitor(node), std::runtime_error);
        }

    } // namespace
} // namespace battery_monitor
