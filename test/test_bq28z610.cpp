#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "battery_monitor/bq28z610.hpp"
#include "fake_i2c_transport.hpp"

namespace battery_monitor {
    namespace {

        using test::FakeI2cTransport;

        class Bq28z610Test : public ::testing::Test {
        protected:
            void SetUp() override {
                auto transport = std::make_unique<FakeI2cTransport>();
                transport_ = transport.get();
                fill_default_registers();
                driver_ = std::make_unique<Bq28z610>(std::move(transport));
            }

            void fill_default_registers() {
                transport_->registers[0x06] = 2981; // 0.1 K -> 24.95 °C
                transport_->registers[0x08] = 7800; // mV
                transport_->registers[0x0A] = 0; // BatteryStatus
                transport_->registers[0x0C] = static_cast<uint16_t>(-1200); // mA
                transport_->registers[0x10] = 2000; // mAh
                transport_->registers[0x12] = 2500; // mAh
                transport_->registers[0x2C] = 80; // %
                transport_->registers[0x3C] = 3000; // mAh
                transport_->mac_payloads[0x0071] =
                    FakeI2cTransport::make_dastatus1_payload(3900, 3850);
                transport_->mac_payloads[0x004E] = {0x34, 0x12}; // 0x1234
            }

            FakeI2cTransport* transport_{nullptr};
            std::unique_ptr<Bq28z610> driver_;
        };

        TEST_F(Bq28z610Test, ReadsPackStateWithRosCurrentSign) {
            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            EXPECT_TRUE(reading->present);
            EXPECT_DOUBLE_EQ(7.8, reading->voltage);
            // 芯片正值为充电，与 sensor_msgs 一致，-1200 mA 是放电。
            EXPECT_DOUBLE_EQ(-1.2, reading->current);
            EXPECT_NEAR(24.95, reading->temperature, 1e-9);
            EXPECT_DOUBLE_EQ(2.0, reading->charge);
            EXPECT_DOUBLE_EQ(2.5, reading->capacity);
            EXPECT_DOUBLE_EQ(3.0, reading->design_capacity);
            EXPECT_DOUBLE_EQ(0.8, reading->percentage);
            EXPECT_EQ(PowerSupplyStatus::DISCHARGING, reading->status);
            EXPECT_EQ(PowerSupplyHealth::GOOD, reading->health);
            EXPECT_EQ(PowerSupplyTechnology::LION, reading->technology);
            ASSERT_EQ(2U, reading->cell_voltage.size());
            EXPECT_DOUBLE_EQ(3.9, reading->cell_voltage[0]);
            EXPECT_DOUBLE_EQ(3.85, reading->cell_voltage[1]);
            EXPECT_TRUE(reading->cell_temperature.empty());
            EXPECT_EQ("0x1234", reading->serial_number);
        }

        TEST_F(Bq28z610Test, FullyChargedAndOvertemperatureFlagsWin) {
            transport_->registers[0x0A] =
                static_cast<uint16_t>((1U << 5) | (1U << 12));

            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            EXPECT_EQ(PowerSupplyStatus::FULL, reading->status);
            EXPECT_EQ(PowerSupplyHealth::OVERHEAT, reading->health);
        }

        TEST_F(Bq28z610Test, OverchargedFlagMapsToOvervoltage) {
            transport_->registers[0x0A] = static_cast<uint16_t>(1U << 15);
            transport_->registers[0x0C] = 0;

            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            EXPECT_EQ(PowerSupplyStatus::NOT_CHARGING, reading->status);
            EXPECT_EQ(PowerSupplyHealth::OVERVOLTAGE, reading->health);
        }

        TEST_F(Bq28z610Test, PositiveCurrentMapsToCharging) {
            transport_->registers[0x0C] = 1500;

            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            EXPECT_DOUBLE_EQ(1.5, reading->current);
            EXPECT_EQ(PowerSupplyStatus::CHARGING, reading->status);
        }

        TEST_F(Bq28z610Test, MacFailureLeavesCellVoltageEmpty) {
            transport_->mac_payloads.clear();

            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            EXPECT_TRUE(reading->cell_voltage.empty());
            EXPECT_TRUE(reading->serial_number.empty());
            EXPECT_DOUBLE_EQ(7.8, reading->voltage);
        }

        TEST_F(Bq28z610Test, MissingRequiredRegisterReturnsError) {
            transport_->registers.erase(0x08);

            const auto reading = driver_->read();

            ASSERT_FALSE(reading.has_value());
            EXPECT_NE(std::string::npos, reading.error().message.find("voltage"));
            EXPECT_NE(std::string::npos, reading.error().message.find("0x08"));
        }

        TEST_F(Bq28z610Test, ZeroSecondCellVoltageIsOmitted) {
            transport_->mac_payloads[0x0071] =
                FakeI2cTransport::make_dastatus1_payload(3900, 0);

            const auto reading = driver_->read();

            ASSERT_TRUE(reading.has_value()) << reading.error().message;
            ASSERT_EQ(1U, reading->cell_voltage.size());
            EXPECT_DOUBLE_EQ(3.9, reading->cell_voltage[0]);
        }

    } // namespace
} // namespace battery_monitor
