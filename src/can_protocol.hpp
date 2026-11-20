#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

constexpr std::uint32_t kTemperatureCanId = 0x3C0;
constexpr std::uint8_t kMaximumSensorId = 107;
constexpr double kDefaultTemperatureStepC = 0.001;

// Selects how the two temperature bytes are assembled into one raw value.
enum class CanByteOrder {
    LittleEndian,
    BigEndian,
};

struct CanFrame {
    std::uint32_t id = 0;
    std::uint8_t length = 0;
    bool isExtended = false;
    bool isRemote = false;
    bool isError = false;
    bool isFd = false;
    std::array<std::uint8_t, 8> data{};
};

struct SensorReading {
    std::string sensorId;
    double temperatureC = 0.0;
};

// Decodes one SIGN-MAGNITUDE temperature using the selected byte order.
double decodeTemperature(std::uint8_t firstByte, std::uint8_t secondByte,
                         CanByteOrder byteOrder = CanByteOrder::LittleEndian,
                         double temperatureStep = kDefaultTemperatureStepC) noexcept;

// Validates a protocol frame and returns its zero, one, or two sensor readings.
bool decodeCanFrame(const CanFrame& frame,
                    std::vector<SensorReading>& readings,
                    CanByteOrder byteOrder = CanByteOrder::LittleEndian);
bool decodeCanFrame(const CanFrame& frame,
                    std::vector<SensorReading>& readings,
                    CanByteOrder byteOrder,
                    const std::vector<std::uint32_t>& acceptedCanIds,
                    double temperatureStep = kDefaultTemperatureStepC);
