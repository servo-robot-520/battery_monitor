#include "battery_monitor/fake_bms.hpp"

namespace battery_monitor {

    std::string FakeBms::name() const {
        return "fake";
    }

    std::expected<BatteryReading, BmsError> FakeBms::read() {
        BatteryReading reading;
        reading.present = true;
        reading.voltage = 7.80;
        reading.current = 0.0;
        reading.charge = 2.0;
        reading.capacity = 2.5;
        reading.design_capacity = 2.5;
        reading.percentage = 0.8;
        reading.temperature = 25.0;
        reading.cell_voltage = {3.90, 3.90};
        reading.cell_temperature = {24.5, 24.8};
        reading.serial_number = "FAKE-0001";
        reading.status = PowerSupplyStatus::NOT_CHARGING;
        reading.health = PowerSupplyHealth::GOOD;
        reading.technology = PowerSupplyTechnology::LION;
        return reading;
    }

} // namespace battery_monitor
