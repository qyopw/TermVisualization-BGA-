#include "can_protocol.hpp"

namespace {

// Adds a reading for an occupied in-range slot; invalid IDs leave peers alone.
void appendReading(std::uint8_t sensorId, std::uint8_t firstTemperatureByte,
                   std::uint8_t secondTemperatureByte, CanByteOrder byteOrder,
                   std::vector<SensorReading>& readings) {
    if (sensorId == 0 || sensorId > kMaximumSensorId) {
        return;
    }

    readings.push_back({std::to_string(sensorId),
                        decodeTemperature(firstTemperatureByte,
                                          secondTemperatureByte, byteOrder)});
}

}  // namespace

// Assembles the selected byte order, then decodes sign and millidegrees.
double decodeTemperature(std::uint8_t firstByte, std::uint8_t secondByte,
                         CanByteOrder byteOrder) noexcept {
    const std::uint16_t rawValue = byteOrder == CanByteOrder::BigEndian
        ? static_cast<std::uint16_t>(
              (static_cast<std::uint16_t>(firstByte) << 8) | secondByte)
        : static_cast<std::uint16_t>(
              firstByte | (static_cast<std::uint16_t>(secondByte) << 8));
    const std::uint16_t magnitude = rawValue & 0x7FFFu;
    const double absoluteTemperature =
        static_cast<double>(magnitude) * kTemperatureScaleC;
    return (rawValue & 0x8000u) != 0 ? -absoluteTemperature
                                    : absoluteTemperature;
}

// Validates one protocol frame and decodes its two sensor slots independently.
bool decodeCanFrame(const CanFrame& frame,
                    std::vector<SensorReading>& readings,
                    CanByteOrder byteOrder) {
    readings.clear();
    if (frame.id != kTemperatureCanId || frame.isExtended || frame.isRemote ||
        frame.isError || frame.isFd || frame.length != 8) {
        return false;
    }

    // Each slot is independent: an empty or invalid ID in one slot does not
    // prevent the other slot from updating.
    appendReading(frame.data[0], frame.data[1], frame.data[2], byteOrder, readings);
    appendReading(frame.data[3], frame.data[4], frame.data[5], byteOrder, readings);
    return true;
}
