import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_config = os.path.join(
        get_package_share_directory("battery_monitor"), "config", "battery_monitor.yaml"
    )
    config = LaunchConfiguration("config")
    namespace = LaunchConfiguration("namespace")

    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "config",
                default_value=default_config,
                description=(
                    "Path to the battery_monitor parameter file; "
                    "defaults to the package configuration"
                ),
            ),
            DeclareLaunchArgument(
                "namespace",
                default_value="",
                description="ROS namespace for the battery_monitor node; empty by default",
            ),
            Node(
                package="battery_monitor",
                executable="battery_monitor_node",
                name="battery_monitor",
                namespace=namespace,
                output="screen",
                parameters=[config],
            ),
        ]
    )
