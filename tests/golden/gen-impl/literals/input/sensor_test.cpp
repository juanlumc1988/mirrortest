#include <gtest/gtest.h>
#include "sensor.h"

TEST(SensorTest, TakesEveryKindOfLiteral) {
    Sensor sensor;
    sensor.set_int(1);
    sensor.set_unsigned(2u);
    sensor.set_long(3l);
    sensor.set_unsigned_long(4ul);
    sensor.set_long_long(5ll);
    sensor.set_unsigned_long_long(6ull);
    sensor.set_hex(0xFF);
    sensor.set_binary(0b101);
    sensor.set_digits(1'000'000);
    sensor.set_negative(-7);
    sensor.set_float(1.5f);
    sensor.set_double(2.5);
    sensor.set_long_double(3.5l);
    sensor.set_char('a');
    sensor.set_bool(true);
    sensor.set_text("text");
    EXPECT_EQ(sensor.get_unsigned(), 2u);
    EXPECT_EQ(sensor.get_long(), 3l);
    EXPECT_EQ(sensor.get_float(), 1.5f);
    EXPECT_EQ(sensor.get_char(), 'a');
    EXPECT_EQ(sensor.get_negative(), -7);
}
