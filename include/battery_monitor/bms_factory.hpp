#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "battery_monitor/bms.hpp"

namespace battery_monitor {

    struct BmsConfig {
        /// 芯片标识：bq28z610 或 fake。
        std::string chip{"bq28z610"};
        /// i2c-dev 设备路径，例如 /dev/i2c-5。
        std::string i2c_device{"/dev/i2c-5"};
        /// 7 位从机地址，BQ28Z610 默认 0x55。
        uint8_t i2c_address{0x55};
    };

    /// 根据配置创建具体 BMS 驱动。
    ///
    /// 新增芯片时在实现中增加对应分支，并在 chip 名称列表中登记；
    /// 节点和消息转换层无需修改。
    std::expected<std::unique_ptr<Bms>, BmsError> create_bms(const BmsConfig& config);

} // namespace battery_monitor
