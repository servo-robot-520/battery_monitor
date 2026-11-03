# battery_monitor

[English](README.md) | [简体中文](README.zh-CN.md)

ROS 2 battery monitor for the TI **BQ28Z610** fuel gauge. It publishes
`sensor_msgs/msg/BatteryState` at 1 Hz. The I2C transport, BMS chip and ROS node
are separated, so the node does not depend on a specific gauge. The BQ28Z610
driver hardcodes its 7-bit address `0x55`; the default bus is `/dev/i2c-5`.
A deterministic fake BMS is available when no battery is connected.

## Features

- Publishes `/battery_state` at 1 Hz and maps every `BatteryState` field.
- `Bms` abstraction plus a factory selected by `bms.chip`; a new gauge only
  needs a new driver implementation.
- BQ28Z610: pack voltage/current/temperature/capacity/SOC, status/health,
  per-cell voltage and pack serial number.
- Per-cell temperature is optional. BQ28Z610 does not measure it, so the array
  stays empty; other BMS drivers may fill it.
- Composition instead of inheritance: `BatteryMonitorNode` holds an external
  `rclcpp::Node`, so other nodes can embed it directly.
- Two build outputs: the `battery_monitor` static library and the
  `battery_monitor_node` executable.
- `common_cpp` logging (`spdlog` + ROS bridge); supports colcon, plain CMake
  and the workspace CLion aggregate build.
- Read failures are rate-limited in the log and skipped instead of publishing
  stale values.

## Hardware and connection

BQ28Z610 uses a fixed 7-bit I2C address of `0x55`, hardcoded in the driver; the
development board exposes the battery bus as `/dev/i2c-5`.

```yaml
bms:
  chip: bq28z610
  i2c_device: /dev/i2c-5
```

The observed board permission is `crw-rw---- root i2c`, and the `cat` user is
not in the `i2c` group. Grant access before running the node:

```bash
sudo usermod -aG i2c cat   # log out and back in afterwards
# or install a udev rule / run the node as root
```

## Build

### colcon

The workspace root itself is a CMake package, so colcon must be pointed at
`src` explicitly:

```bash
cd microduck_rev
source /opt/ros/humble/setup.bash
export CC=/usr/bin/gcc-13 CXX=/usr/bin/g++-13

colcon build --base-paths src --packages-select common_cpp battery_monitor \
  --cmake-args -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
colcon test --base-paths src --packages-select battery_monitor
colcon test-result --test-result-base build/battery_monitor/test_results --verbose
```

`common_cpp` requires a C++23 standard library (GCC 13+), so use GCC 13 for the
whole workspace.

### Plain CMake / CLion

`common_cpp` must be built and installed first. Then:

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

> Within a quoted CMake argument use `;` to separate prefixes, not `:`.

For CLion, open the workspace root, select GCC 13 and load the ROS 2
environment. The root `CMakeLists.txt` sets `ROS2_WS_SOURCE_BUILD=ON` and adds
`battery_monitor` to `colcon_add_subdirectories(...)`; the package then links
the in-tree `common_cpp` targets. Add `-DBUILD_TESTING=ON` to the CLion CMake
options to build the tests.

## Run

Hardware configuration:

```bash
ros2 launch battery_monitor battery_monitor.launch.py
```

Fake BMS on a laptop or CI machine:

```bash
ros2 launch battery_monitor battery_monitor.launch.py \
  config:=$(ros2 pkg prefix battery_monitor)/share/battery_monitor/config/battery_monitor_fake.yaml
```

Run the executable directly with a parameter file:

```bash
ros2 run battery_monitor battery_monitor_node --ros-args \
  --params-file "$(ros2 pkg prefix battery_monitor)/share/battery_monitor/config/battery_monitor.yaml"
```

Verify the output:

```bash
ros2 topic echo /battery_state --once
ros2 topic hz /battery_state
```

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `bms.chip` | string | `bq28z610` | Driver name: `bq28z610` or `fake` |
| `bms.i2c_device` | string | `/dev/i2c-5` | i2c-dev path; the slave address is fixed per driver |
| `publish_rate_hz` | double | `1.0` | Publish rate, `(0, 1000]` |
| `frame_id` | string | `""` | `BatteryState.header.frame_id`; empty when no TF frame exists |
| `location` | string | `battery` | `BatteryState.location` |
| `log.log_level` | string | `info` | `common_cpp` log level |
| `log.log_path` | string | `./log` | `common_cpp` log directory |
| `log.async` | bool | `false` | Asynchronous spdlog backend |
| `log.logger_name` | string | node name | spdlog logger name |
| `log.filter.black` | string[] | `[]` | Message filters |

All parameters are declared on the node passed to `BatteryMonitorNode`, so a
composing node must not already use the same names with incompatible types.

## Published message

Topic: `battery_state` (relative to the node namespace, `/battery_state` by
default). Type: `sensor_msgs/msg/BatteryState`.

| Field | Source on BQ28Z610 | Notes |
|---|---|---|
| `header.stamp` | `node->now()` | ROS time |
| `header.frame_id` | `frame_id` parameter | Empty by default |
| `voltage` | `Voltage()` `0x08` | mV → V |
| `temperature` | `Temperature()` `0x06` | 0.1 K → °C, pack sensor |
| `current` | `Current()` `0x0C` | mA → A, positive when charging |
| `charge` | `RemainingCapacity()` `0x10` | mAh → Ah |
| `capacity` | `FullChargeCapacity()` `0x12` | mAh → Ah |
| `design_capacity` | `DesignCapacity()` `0x3C` | mAh → Ah |
| `percentage` | `RelativeStateOfCharge()` `0x2C` | 0..100 → 0..1 |
| `power_supply_status` | BatteryStatus flags + current sign | FC → `FULL`, sign ±50 mA deadband |
| `power_supply_health` | BatteryStatus OTA/OCA | `OVERHEAT` / `OVERVOLTAGE`, otherwise `GOOD` |
| `power_supply_technology` | driver | `LION` |
| `present` | successful read | `true` |
| `cell_voltage` | MAC `0x0071` DAStatus1 | Cells reporting 0 mV are omitted |
| `cell_temperature` | BMS driver | Empty for BQ28Z610 |
| `location` | `location` parameter | `battery` by default |
| `serial_number` | MAC `0x004E` | `0xXXXX`; empty if the read fails |

## BMS abstraction and register map

`BatteryReading` carries normalized values and optional per-cell arrays. A
driver fills the fields it can measure and leaves unsupported optional arrays
empty; the node copies them to the message without chip-specific logic.

The BQ28Z610 uses the bq27xxx command map, not the generic SBS map:

```text
Temperature 0x06   Voltage 0x08   BatteryStatus 0x0A   Current 0x0C
RemainingCapacity 0x10   FullChargeCapacity 0x12   SOC 0x2C
DesignCapacity 0x3C      MAC 0x3E..0x61
```

`docs/BQ28Z610-寄存器手册.md` lists the generic SBS addresses (for example
voltage `0x09` and SOC `0x0D`). Those addresses do not match the TI TRM and must
not be used. The authoritative source is
`docs/BQ28Z620-R1TechnicalReferenceManual.pdf`, Table 14-1.

MAC responses are read as one 36-byte block starting at `0x3E`: two bytes of
subcommand echo, up to 32 data bytes, then checksum and length. Per-cell
voltages come from DAStatus1 (`0x0071`); the serial number comes from `0x004E`.

## Adding another BMS chip

1. Implement `BatteryReading Bms::read()` in a new driver class; keep I2C
   access behind `I2cTransport` so it can be tested with a fake slave. Expose
   the chip's fixed 7-bit I2C address as a public constant.
2. Add the chip name and constructor branch to `create_bms()` in
   `src/bms_factory.cpp`, passing that address to `LinuxI2cTransport`.
3. Add a config YAML and unit tests; the node and message conversion stay
   unchanged.

## Testing with the fake BMS

`FakeBms` returns fixed, plausible values and needs no device:

```yaml
bms:
  chip: fake
```

The unit tests also use `FakeI2cTransport`, an in-memory BQ28Z610 register map,
to cover decoding without hardware:

- register conversion, current sign, status/health mapping, serial number;
- MAC checksum validation and graceful degradation when optional reads fail;
- chip-name normalization, invalid addresses and missing devices;
- fake-mode node publication and full message mapping.

## Limitations and deployment notes

- BQ28Z610 has no per-cell temperature; `cell_temperature` stays empty. Set
  each entry to NaN in a future driver if a cell count is known but values are
  unavailable.
- No battery TF frame is defined in this workspace, so `frame_id` defaults to
  an empty string. Set it in the parameter file if consumers need a frame.
- The 36-byte MAC read and serial-number command have not been verified on a
  real battery yet; no device was connected during development. The register
  decode is covered by unit tests only.
- The serial number can be the data-flash default `0x0001` if the pack was not
  programmed.
- The node opens the I2C device once at construction. Bus errors are retried on
  the next timer tick; the transport is not reopened.
- One node instance manages one pack. Use separate namespaces (and I2C
  addresses) for multiple packs.
- No charging control, balancing or SOC learning is performed; the node is
  read-only.

## License

GPL-3.0-only. See `LICENSE`.
