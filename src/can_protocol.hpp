#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct CanFrame {
    std::uint32_t id = 0;
    std::uint8_t length = 0;
    bool isExtended = false;
    std::array<std::uint8_t, 8> data{};
};

struct CanFilterId {
    std::uint32_t id = 0;
    bool isExtended = false;
};

struct SensorReading {
    std::string sensorId;
    double temperatureC = 0.0;
};

// A frame ID selects a group of eight sensors; each data byte is one
// unsigned whole-degree Celsius reading. Extended IDs also accept the
// packed-decimal ID spelling used in the PCAN-View XMT file.
bool buildCanFiltersForSensors(const std::vector<std::string>& sensorIds,
                               std::vector<CanFilterId>& filters,
                               std::string& error);
bool decodeCanFrame(const CanFrame& frame,
                    std::vector<SensorReading>& readings);
