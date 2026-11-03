#pragma once

#include <expected>

#include "battery_monitor/bms.hpp"

namespace battery_monitor {

    /// 无硬件时使用的确定性电池模拟器。
    ///
    /// 通过 bms.chip=fake 选择，节点可以完整跑通定时读取、消息转换和发布链路。
    /// 数值固定，方便在笔记本上观察/断言，不用于验证真实 BMS 协议。
    class FakeBms final : public Bms {
    public:
        FakeBms() = default;

        [[nodiscard]] std::string name() const override;

        std::expected<BatteryReading, BmsError> read() override;
    };

} // namespace battery_monitor
