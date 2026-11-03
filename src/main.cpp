#include <exception>
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "battery_monitor/battery_monitor_node.hpp"
#include "common_cpp/logging/rclcpp_log_handler.hpp"

int main(const int argc, char** argv) {
    rclcpp::init(argc, argv);

    const auto node = std::make_shared<rclcpp::Node>("battery_monitor");
    // rcutils 输出处理器是进程级对象，只能由可执行文件安装一次。
    if (!logging::ros::RclcppLogHandler::try_init_from_node(node)) {
        return 1;
    }

    try {
        const auto monitor = std::make_shared<battery_monitor::BatteryMonitorNode>(node);

        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
        executor.remove_node(node);
    }
    catch (const std::exception& exception) {
        RCLCPP_FATAL(node->get_logger(), "Battery monitor terminated: %s", exception.what());
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    }
    catch (...) {
        RCLCPP_FATAL(node->get_logger(), "Battery monitor terminated with an unknown error");
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    }

    logging::ros::RclcppLogHandler::shutdown();
    rclcpp::shutdown();
    return 0;
}
