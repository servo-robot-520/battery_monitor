# battery_monitor

[English](README.md) | [简体中文](README.zh-CN.md)

面向 TI **BQ28Z610** 电量计的 ROS 2 电池监控节点，以 1 Hz 发布
`sensor_msgs/msg/BatteryState`。I2C 传输、BMS 芯片与 ROS 节点分层，节点不依赖
具体芯片。默认硬件配置使用 `/dev/i2c-5`、7 位地址 `0x55`；未连接电池时可切换到
确定性的 fake BMS。

## 特性

- 1 Hz 发布 `/battery_state`，映射 `BatteryState` 的全部字段。
- `Bms` 抽象 + 工厂，按 `bms.chip` 选择具体芯片；新增芯片不影响节点。
- BQ28Z610：整包电压/电流/温度/容量/SOC、状态/健康、单体电压与电池包序列号。
- 单体温度可选。BQ28Z610 不测量单体温度，数组保持为空；其他 BMS 驱动可以填充。
- 组合而不是继承：`BatteryMonitorNode` 持有外部 `rclcpp::Node`，可被其他节点
  直接嵌入。
- 两个构建输出：`battery_monitor` 静态库与 `battery_monitor_node` 可执行文件。
- 集成 `common_cpp` 日志（spdlog + ROS 桥接）；支持 colcon、普通 CMake 和
  顶层 CLion 聚合构建。
- 读取失败限频记录并跳过发布，避免把陈旧数据当成实时状态。

## 包结构

```text
battery_monitor/
├─ include/battery_monitor/
│  ├─ bms.hpp                     # BatteryReading、Bms 接口与枚举
│  ├─ bms_factory.hpp             # 芯片与 I2C 配置
│  ├─ bq28z610.hpp                # TI BQ28Z610 驱动
│  ├─ fake_bms.hpp                # 确定性模拟器
│  ├─ i2c_transport.hpp           # SMBus/原始 I2C 抽象 + Linux 实现
│  └─ battery_monitor_node.hpp    # 组合式 ROS 节点
├─ src/
│  ├─ bms_factory.cpp
│  ├─ bq28z610.cpp
│  ├─ fake_bms.cpp
│  ├─ i2c_transport.cpp
│  ├─ battery_monitor_node.cpp
│  └─ main.cpp
├─ config/
│  ├─ battery_monitor.yaml        # /dev/i2c-5 上的 BQ28Z610
│  └─ battery_monitor_fake.yaml   # 笔记本/CI 使用的 fake BMS
├─ launch/battery_monitor.launch.py
├─ test/
│  ├─ fake_i2c_transport.hpp
│  ├─ test_bq28z610.cpp
│  ├─ test_bms_factory.cpp
│  └─ test_battery_monitor_node.cpp
├─ CMakeLists.txt
└─ package.xml
```

## 硬件与连接

BQ28Z610 的 7 位 I2C 地址为 `0x55`，开发板上电池总线对应 `/dev/i2c-5`。

```yaml
bms:
  chip: bq28z610
  i2c_device: /dev/i2c-5
  i2c_address: 85
```

当前开发板的设备权限为 `crw-rw---- root i2c`，`cat` 用户不在 `i2c` 组。运行前
需要授权：

```bash
sudo usermod -aG i2c cat   # 之后重新登录
# 或安装 udev 规则 / 以 root 运行节点
```

## 构建

### colcon

工作区根目录本身也是一个 CMake 包，因此必须显式指定 `src`：

```bash
cd microduck_rev
source /opt/ros/humble/setup.bash
export CC=/usr/bin/gcc-13 CXX=/usr/bin/g++-13

colcon build --base-paths src --packages-select common_cpp battery_monitor \
  --cmake-args -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
colcon test --base-paths src --packages-select battery_monitor
colcon test-result --test-result-base build/battery_monitor/test_results --verbose
```

`common_cpp` 需要支持 C++23 的标准库（GCC 13+），因此整个工作区应使用 GCC 13。

### 普通 CMake / CLion

先构建并安装 `common_cpp`，然后：

```bash
cd microduck_rev
source /opt/ros/humble/setup.bash
source install/setup.bash

cmake -S src/battery_monitor -B build-battery \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DCMAKE_PREFIX_PATH="$PWD/install/common_cpp;/opt/ros/humble" \
  -DBUILD_TESTING=ON
cmake --build build-battery -j
ctest --test-dir build-battery --output-on-failure
```

> 带引号的 CMake 参数中用 `;` 分隔多个前缀，不要用 `:`。

使用 CLion 时打开工作区根目录，选择 GCC 13 并加载 ROS 2 环境。根
`CMakeLists.txt` 会设置 `ROS2_WS_SOURCE_BUILD=ON`，并把 `battery_monitor` 加入
`colcon_add_subdirectories(...)`；此时本包链接构建树中的 `common_cpp` target。
如需构建测试，请在 CLion 的 CMake 选项中添加 `-DBUILD_TESTING=ON`。

## 运行

硬件配置：

```bash
ros2 launch battery_monitor battery_monitor.launch.py
```

笔记本或 CI 上使用 fake BMS：

```bash
ros2 launch battery_monitor battery_monitor.launch.py \
  config:=$(ros2 pkg prefix battery_monitor)/share/battery_monitor/config/battery_monitor_fake.yaml
```

也可以直接带参数文件运行：

```bash
ros2 run battery_monitor battery_monitor_node --ros-args \
  --params-file "$(ros2 pkg prefix battery_monitor)/share/battery_monitor/config/battery_monitor.yaml"
```

检查发布结果：

```bash
ros2 topic echo /battery_state --once
ros2 topic hz /battery_state
```

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `bms.chip` | string | `bq28z610` | 驱动名称：`bq28z610` 或 `fake` |
| `bms.i2c_device` | string | `/dev/i2c-5` | i2c-dev 设备路径 |
| `bms.i2c_address` | int | `85`（`0x55`） | 7 位地址，`0x03`..`0x77` |
| `publish_rate_hz` | double | `1.0` | 发布频率，`(0, 1000]` |
| `frame_id` | string | `""` | `BatteryState.header.frame_id`；无 TF 坐标系时为空 |
| `location` | string | `battery` | `BatteryState.location` |
| `log.log_level` | string | `info` | `common_cpp` 日志级别 |
| `log.log_path` | string | `./log` | `common_cpp` 日志目录 |
| `log.async` | bool | `false` | 异步 spdlog 后端 |
| `log.logger_name` | string | 节点名 | spdlog logger 名称 |
| `log.filter.black` | string[] | `[]` | 日志过滤规则 |

所有参数都声明在传入 `BatteryMonitorNode` 的节点上；组合到其他节点时，不要
重复声明同名但类型不兼容的参数。

## 发布消息

话题：`battery_state`（相对节点命名空间，默认 `/battery_state`）。类型：
`sensor_msgs/msg/BatteryState`。

| 字段 | BQ28Z610 来源 | 说明 |
|---|---|---|
| `header.stamp` | `node->now()` | ROS 时间 |
| `header.frame_id` | `frame_id` 参数 | 默认空 |
| `voltage` | `Voltage()` `0x08` | mV → V |
| `temperature` | `Temperature()` `0x06` | 0.1 K → °C，整包温度 |
| `current` | `Current()` `0x0C` | mA → A，正值为充电 |
| `charge` | `RemainingCapacity()` `0x10` | mAh → Ah |
| `capacity` | `FullChargeCapacity()` `0x12` | mAh → Ah |
| `design_capacity` | `DesignCapacity()` `0x3C` | mAh → Ah |
| `percentage` | `RelativeStateOfCharge()` `0x2C` | 0..100 → 0..1 |
| `power_supply_status` | BatteryStatus 标志 + 电流方向 | FC → `FULL`，±50 mA 死区 |
| `power_supply_health` | BatteryStatus OTA/OCA | `OVERHEAT` / `OVERVOLTAGE`，否则 `GOOD` |
| `power_supply_technology` | 驱动 | `LION` |
| `present` | 读取成功 | `true` |
| `cell_voltage` | MAC `0x0071` DAStatus1 | 0 mV 的单体被省略 |
| `cell_temperature` | BMS 驱动 | BQ28Z610 为空 |
| `location` | `location` 参数 | 默认 `battery` |
| `serial_number` | MAC `0x004E` | `0xXXXX`；读取失败留空 |

## BMS 抽象与寄存器表

`BatteryReading` 保存归一化数值和可选单体数组。具体驱动只填自己能测量的字段，
不支持的可选数组留空；节点原样映射，不包含芯片相关分支。

BQ28Z610 使用 bq27xxx 命令表，而不是通用 SBS 地址：

```text
Temperature 0x06   Voltage 0x08   BatteryStatus 0x0A   Current 0x0C
RemainingCapacity 0x10   FullChargeCapacity 0x12   SOC 0x2C
DesignCapacity 0x3C      MAC 0x3E..0x61
```

`docs/BQ28Z610-寄存器手册.md` 列的是通用 SBS 地址（例如电压 `0x09`、SOC `0x0D`），
与 TI TRM 不一致，不能直接用于本芯片。权威来源是
`docs/BQ28Z620-R1TechnicalReferenceManual.pdf` 的 Table 14-1。

MAC 响应从 `0x3E` 起一次读取 36 字节：前两字节是子命令回显，随后最多 32 字节
数据，最后两字节是校验和与长度。单体电压来自 DAStatus1（`0x0071`），序列号来自
`0x004E`。

## 扩展新芯片

1. 新建驱动类实现 `Bms::read()`；I2C 访问保持在 `I2cTransport` 之后，便于用
   假从机测试。
2. 在 `src/bms_factory.cpp` 的 `create_bms()` 中加入芯片名称和构造分支。
3. 添加配置 YAML 与单元测试；节点和消息转换层无需修改。

## 使用 fake BMS 测试

`FakeBms` 返回固定、合理的数值，不需要任何设备：

```yaml
bms:
  chip: fake
```

单元测试还使用 `FakeI2cTransport`（内存中的 BQ28Z610 寄存器表）覆盖无硬件解码：

- 寄存器换算、电流符号、状态/健康映射、序列号；
- MAC 校验和校验，以及可选读取失败时的降级；
- 芯片名归一化、非法地址、设备不存在；
- fake 模式下的节点发布与完整消息映射。

## 限制与部署注意

- BQ28Z610 没有单体温度，`cell_temperature` 保持为空。若将来找到电芯数量但
  无法测量温度，可在驱动中按消息约定逐芯填 NaN。
- 本工作区没有定义电池所在的 TF 坐标系，因此 `frame_id` 默认空；需要坐标变换的
  消费方请在参数文件中设置。
- 36 字节 MAC 读取与序列号命令尚未在真实电池上验证；开发时未连接设备。寄存器
  解码目前只有单元测试覆盖。
- 如果电池包未烧录序列号，读到的可能是数据闪存默认值 `0x0001`。
- 节点在构造时打开一次 I2C 设备。总线错误会在下一个定时周期重试，但不会重新
  打开设备。
- 一个节点实例管理一个电池包。多个电池包请使用不同命名空间与 I2C 地址。
- 节点只读，不执行充电控制、均衡或 SOC 学习。

## License

GPL-3.0-only，详见 `LICENSE`。
