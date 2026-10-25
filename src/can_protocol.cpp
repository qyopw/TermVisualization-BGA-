#include "can_protocol.hpp"

#include <charconv>
#include <string>
#include <system_error>

bool parseSensorCanId(const std::string& sensorId, std::uint32_t& canId) {
    if (sensorId.empty()) {
        return false;
    }
    std::uint32_t parsed = 0;
    const char* const begin = sensorId.data();
    const char* const end = begin + sensorId.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end || parsed > 0x7FF ||
        std::to_string(parsed) != sensorId) {
        return false;
    }
    canId = parsed;
    return true;
}

bool decodeCanFrame(const CanFrame& frame, SensorReading& reading) {
    if (frame.id > 0x7FF || frame.length < 2 || frame.length > 8) {
        return false;
    }

    const std::uint16_t unsignedValue =
        static_cast<std::uint16_t>(frame.data[0]) |
        (static_cast<std::uint16_t>(frame.data[1]) << 8);
    const std::int32_t signedValue = unsignedValue < 0x8000
        ? static_cast<std::int32_t>(unsignedValue)
        : static_cast<std::int32_t>(unsignedValue) - 0x10000;

    reading.sensorId = std::to_string(frame.id);
    reading.temperatureC = static_cast<double>(signedValue) / 100.0;
    return true;
}
