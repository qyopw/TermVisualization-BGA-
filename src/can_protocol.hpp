#pragma once

#include <array>
#include <cstdint>
#include <string>

struct CanFrame {
    std::uint32_t id = 0;
    std::uint8_t length = 0;
    std::array<std::uint8_t, 8> data{};
};

struct SensorReading {
    std::string sensorId;
    double temperatureC = 0.0;
};

// The 11-bit CAN ID is the decimal sensor ID; data[0..1] is a signed
// little-endian temperature in hundredths of a degree C.
bool parseSensorCanId(const std::string& sensorId, std::uint32_t& canId);
bool decodeCanFrame(const CanFrame& frame, SensorReading& reading);
