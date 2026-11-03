#include <gtest/gtest.h>

#include <string>

#include "battery_monitor/bms_factory.hpp"

namespace battery_monitor {
    namespace {

        TEST(BmsFactoryTest, CreatesFakeBmsAndNormalizesChipName) {
            BmsConfig config;
            config.chip = "  FAKE ";

            const auto bms = create_bms(config);

            ASSERT_TRUE(bms.has_value()) << bms.error().message;
            EXPECT_EQ("fake", bms.value()->name());
            const auto reading = bms.value()->read();
            ASSERT_TRUE(reading.has_value());
            EXPECT_TRUE(reading->present);
            EXPECT_TRUE(reading->cell_voltage.size() == 2);
        }

        TEST(BmsFactoryTest, RejectsUnknownChip) {
            BmsConfig config;
            config.chip = "bq99999";

            const auto bms = create_bms(config);

            ASSERT_FALSE(bms.has_value());
            EXPECT_NE(std::string::npos, bms.error().message.find("unsupported bms chip"));
            EXPECT_NE(std::string::npos, bms.error().message.find("bq28z610"));
            EXPECT_NE(std::string::npos, bms.error().message.find("fake"));
        }

        TEST(BmsFactoryTest, RejectsEmptyI2cDevice) {
            BmsConfig config;
            config.i2c_device.clear();

            const auto bms = create_bms(config);

            ASSERT_FALSE(bms.has_value());
            EXPECT_NE(std::string::npos, bms.error().message.find("bms.i2c_device"));
        }

        TEST(BmsFactoryTest, RejectsReservedI2cAddressBeforeOpeningDevice) {
            BmsConfig config;
            config.i2c_device = "/dev/i2c-does-not-exist";
            config.i2c_address = 0x02;

            const auto bms = create_bms(config);

            ASSERT_FALSE(bms.has_value());
            EXPECT_NE(std::string::npos, bms.error().message.find("0x02"));
            EXPECT_NE(std::string::npos, bms.error().message.find("7-bit"));
        }

        TEST(BmsFactoryTest, ReportsMissingI2cDevice) {
            BmsConfig config;
            config.i2c_device = "/dev/i2c-battery-monitor-missing";

            const auto bms = create_bms(config);

            ASSERT_FALSE(bms.has_value());
            EXPECT_NE(std::string::npos, bms.error().message.find("cannot open"));
            EXPECT_NE(std::string::npos, bms.error().message.find(config.i2c_device));
        }

    } // namespace
} // namespace battery_monitor
