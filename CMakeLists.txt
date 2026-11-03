cmake_minimum_required(VERSION 3.22)
project(battery_monitor VERSION 0.1.0)

option(BUILD_EXE "Build node executable file" ON)
option(BUILD_TESTING "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT SKIP_CXX23_STDLIB_CHECK AND NOT _CXX23_STDLIB_VERIFIED)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_QUIET TRUE)
    string(APPEND CMAKE_REQUIRED_FLAGS " -std=c++23")

    string(TOUPPER "${PROJECT_NAME}" _PRJ_UPPER)

    check_cxx_source_compiles([[
        #include <expected>
        #include <format>
        #include <ranges>
        int main() {
            std::expected<int, int> result{42};
            const auto text = std::format("{}", result.value());
            const auto values = std::views::iota(0, 1);
            return static_cast<int>(text.size() + *values.begin());
        }
    ]] ${_PRJ_UPPER}_HAS_REQUIRED_CXX23_STDLIB)

    if (NOT ${_PRJ_UPPER}_HAS_REQUIRED_CXX23_STDLIB)
        message(FATAL_ERROR
                "${PROJECT_NAME} requires a C++23 standard library with <expected>, "
                "<format>, and ranges support. Use GCC 13+ or an equivalent compiler.")
    endif ()
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# 顶层源码聚合构建（CLion）直接使用本次构建树中的 common_cpp target；
# 普通 colcon/独立 CMake 构建则导入已安装的 common_cpp。
if (NOT ROS2_WS_SOURCE_BUILD)
    find_package(common_cpp REQUIRED)
endif ()

# 电池驱动、BMS 抽象与组合式节点的唯一库目标，其他节点可直接链接。
# 以静态库导出并开启 PIC，既能被可执行文件链接，也能被后续的组件库复用。
add_library(battery_monitor STATIC
        include/battery_monitor/bms.hpp
        include/battery_monitor/bms_factory.hpp
        include/battery_monitor/bq28z610.hpp
        include/battery_monitor/battery_monitor_node.hpp
        include/battery_monitor/fake_bms.hpp
        include/battery_monitor/i2c_transport.hpp
        src/bms_factory.cpp
        src/bq28z610.cpp
        src/battery_monitor_node.cpp
        src/fake_bms.cpp
        src/i2c_transport.cpp
)
target_compile_features(battery_monitor PUBLIC cxx_std_23)
set_target_properties(battery_monitor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(battery_monitor PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(battery_monitor PUBLIC rclcpp sensor_msgs)

if (ROS2_WS_SOURCE_BUILD)
    if (NOT TARGET common_cpp_logging_interface)
        message(FATAL_ERROR
                "CLion workspace configuration requires the in-tree common_cpp_logging_interface target."
                "Open the workspace root and ensure common_cpp is added before battery_monitor.")
    endif ()
    target_link_libraries(battery_monitor PUBLIC common_cpp_logging_interface)
else ()
    target_link_libraries(battery_monitor PUBLIC common_cpp::common_cpp_logging_interface)
endif ()

if (BUILD_EXE)
    add_executable(battery_monitor_node src/main.cpp)
    target_compile_features(battery_monitor_node PRIVATE cxx_std_23)
    target_link_libraries(battery_monitor_node battery_monitor)
    ament_target_dependencies(battery_monitor_node rclcpp)

    # rcutils 输出处理器是进程全局对象，只能由 main.cpp 安装。
    # 不要让库目标链接这一依赖。
    if (ROS2_WS_SOURCE_BUILD)
        if (NOT TARGET common_cpp_ros_logging)
            message(FATAL_ERROR
                    "CLion workspace configuration requires the in-tree common_cpp_ros_logging target."
                    "Open the workspace root and ensure common_cpp is added before battery_monitor.")
        endif ()
        target_link_libraries(battery_monitor_node common_cpp_ros_logging)
    else ()
        target_link_libraries(battery_monitor_node common_cpp::common_cpp_ros_logging)
    endif ()
endif ()

if (BUILD_TESTING)
    find_package(ament_cmake_gtest REQUIRED)

    ament_add_gtest(test_bq28z610 test/test_bq28z610.cpp)
    target_include_directories(test_bq28z610 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_link_libraries(test_bq28z610 battery_monitor)

    ament_add_gtest(test_bms_factory test/test_bms_factory.cpp)
    target_link_libraries(test_bms_factory battery_monitor)

    ament_add_gtest(test_battery_monitor_node test/test_battery_monitor_node.cpp)
    target_link_libraries(test_battery_monitor_node battery_monitor)
endif ()

install(TARGETS battery_monitor
        EXPORT battery_monitorTargets
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

if (BUILD_EXE)
    install(TARGETS battery_monitor_node
            DESTINATION lib/${PROJECT_NAME}
    )
    install(DIRECTORY config launch
            DESTINATION share/${PROJECT_NAME}
            PATTERN "__pycache__" EXCLUDE
            PATTERN "*.pyc" EXCLUDE
    )
    install(FILES README.md README.zh-CN.md
            DESTINATION share/${PROJECT_NAME}
    )
endif ()

install(DIRECTORY include/
        DESTINATION include
)

ament_export_include_directories(include)
ament_export_dependencies(rclcpp sensor_msgs common_cpp)
ament_export_targets(battery_monitorTargets HAS_LIBRARY_TARGET)
ament_package()
