#include "can_protocol.hpp"
#include "sensor_state.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

// Records a failed test condition with its description.
void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Checks floating-point results within the protocol's required precision.
void expectNear(double actual, double expected, const std::string& message) {
    expect(std::abs(actual - expected) < 1e-9, message);
}

// Creates a standard eight-byte frame for a decoder test.
CanFrame makeFrame(std::array<std::uint8_t, 8> data) {
    CanFrame frame;
    frame.id = kTemperatureCanId;
    frame.length = 8;
    frame.data = data;
    return frame;
}

// Decodes protocol reference frames with the explicitly documented 0.001 C step.
bool decodeAtMillidegreeStep(const CanFrame& frame,
                            std::vector<SensorReading>& readings,
                            CanByteOrder byteOrder = CanByteOrder::LittleEndian) {
    static const std::vector<std::uint32_t> defaultCanIds{kTemperatureCanId};
    return decodeCanFrame(frame, readings, byteOrder, defaultCanIds, 0.001);
}

// Checks one decoded sensor ID and temperature.
void expectReading(const std::vector<SensorReading>& readings,
                   std::size_t index, const std::string& sensorId,
                   double temperatureC, const std::string& message) {
    expect(index < readings.size(), message + " (reading missing)");
    if (index < readings.size()) {
        expect(readings[index].sensorId == sensorId,
               message + " (wrong sensor ID)");
        expectNear(readings[index].temperatureC, temperatureC,
                   message + " (wrong temperature)");
    }
}

// Verifies endian assembly, sign handling, zero, and fractional temperatures.
void testTemperatureDecoding() {
    expectNear(decodeTemperature(0x64, 0x00, CanByteOrder::LittleEndian, 0.001), 0.100,
               "Little Endian 0x0064 decodes to +0.100 C");
    expectNear(decodeTemperature(0x64, 0x80, CanByteOrder::LittleEndian, 0.001), -0.100,
               "SIGN-MAGNITUDE 0x8064 decodes to -0.100 C");
    expectNear(decodeTemperature(0x00, 0x00), 0.0,
               "zero decodes to 0 C");
    expectNear(decodeTemperature(0xD2, 0x04, CanByteOrder::LittleEndian, 0.001), 1.234,
               "fractional temperature uses 0.001 C scale and Little Endian");
    expectNear(decodeTemperature(0xE8, 0x03, CanByteOrder::LittleEndian, 0.001), 1.000,
               "0x03E8 decodes to +1.000 C");
    expectNear(decodeTemperature(0xE8, 0x83, CanByteOrder::LittleEndian, 0.001), -1.000,
               "0x83E8 decodes to -1.000 C");
    expectNear(decodeTemperature(0x00, 0x64, CanByteOrder::BigEndian, 0.001), 0.100,
               "Big Endian 0x0064 decodes to +0.100 C");
    expectNear(decodeTemperature(0x80, 0x64, CanByteOrder::BigEndian, 0.001), -0.100,
               "Big Endian SIGN-MAGNITUDE 0x8064 decodes to -0.100 C");
    expectNear(decodeTemperature(0x04, 0xD2, CanByteOrder::BigEndian, 0.001), 1.234,
               "Big Endian fractional temperature decodes correctly");
    expectNear(decodeTemperature(1, 0, CanByteOrder::LittleEndian, 1.0), 1.0,
               "temperature step 1 C is applied");
    expectNear(decodeTemperature(1, 0, CanByteOrder::LittleEndian, 0.1), 0.1,
               "temperature step 0.1 C is applied");
    expectNear(decodeTemperature(1, 0, CanByteOrder::LittleEndian, 0.01), 0.01,
               "temperature step 0.01 C is applied");
    expectNear(decodeTemperature(1, 0), 0.01,
               "default temperature step is 0.01 C");
    expectNear(decodeTemperature(0xFF, 0x7F, CanByteOrder::LittleEndian, 0.001), 32.767,
               "0.001 C step has a maximum of +32.767 C");
    expectNear(decodeTemperature(0xFF, 0xFF, CanByteOrder::LittleEndian, 0.001), -32.767,
               "0.001 C step has a minimum of -32.767 C");
}

// Verifies the two-slot payload layout and slot-level ID handling.
void testProtocolFrames() {
    std::vector<SensorReading> readings;
    CanFrame frame = makeFrame({0x01, 0x64, 0x00, 0x02,
                                0x64, 0x80, 0x00, 0x00});
    expect(decodeAtMillidegreeStep(frame, readings), "control frame 1 is accepted");
    expect(readings.size() == 2, "one frame decodes two occupied slots");
    expectReading(readings, 0, "1", 0.100, "control frame sensor 1");
    expectReading(readings, 1, "2", -0.100, "control frame sensor 2");

    frame = makeFrame({0x05, 0xE8, 0x03, 0x06,
                       0xE8, 0x83, 0x00, 0x00});
    expect(decodeAtMillidegreeStep(frame, readings), "control frame 2 is accepted");
    expect(readings.size() == 2, "second control frame has two readings");
    expectReading(readings, 0, "5", 1.000, "control frame sensor 5");
    expectReading(readings, 1, "6", -1.000, "control frame sensor 6");

    frame = makeFrame({0x01, 0x00, 0x64, 0x02,
                       0x80, 0x64, 0x00, 0x00});
    expect(decodeAtMillidegreeStep(frame, readings, CanByteOrder::BigEndian),
           "Big Endian frame is accepted when selected");
    expect(readings.size() == 2, "Big Endian frame decodes both slots");
    expectReading(readings, 0, "1", 0.100, "Big Endian first sensor");
    expectReading(readings, 1, "2", -0.100, "Big Endian second sensor");

    frame = makeFrame({1, 0x64, 0x00, 0, 0x00, 0x00, 0, 0});
    expect(decodeCanFrame(frame, readings, CanByteOrder::LittleEndian,
                          std::vector<std::uint32_t>{kTemperatureCanId}, 0.01),
           "frame accepts configured temperature step");
    expectReading(readings, 0, "1", 1.0, "frame temperature uses 0.01 C step");

    frame = makeFrame({0x01, 0x00, 0x64, 0x02,
                       0x80, 0x64, 0x00, 0x00});
    frame.id = 0x3C1;
    expect(!decodeCanFrame(frame, readings),
           "non-default CAN ID is rejected unless configured");
    const std::vector<std::uint32_t> configuredCanIds{0x3C0, 0x3C1};
    expect(decodeCanFrame(frame, readings, CanByteOrder::BigEndian,
                          configuredCanIds),
           "configured CAN ID is accepted");
    frame.id = 0x3C2;
    expect(!decodeCanFrame(frame, readings, CanByteOrder::BigEndian,
                           configuredCanIds),
           "CAN ID outside configured list is rejected");

    frame = makeFrame({23, 0xD2, 0x04, 107, 0x64, 0x80, 0xAA, 0x55});
    expect(decodeAtMillidegreeStep(frame, readings), "arbitrary valid sensor IDs are accepted");
    expect(readings.size() == 2, "reserved bytes do not affect valid slots");
    expectReading(readings, 0, "23", 1.234, "arbitrary sensor 23");
    expectReading(readings, 1, "107", -0.100, "upper-bound sensor 107");

    frame = makeFrame({0, 0xFF, 0x7F, 9, 0x2C, 0x01, 0, 0});
    expect(decodeAtMillidegreeStep(frame, readings), "empty slot does not invalidate frame");
    expect(readings.size() == 1, "ID zero leaves its slot empty");
    expectReading(readings, 0, "9", 0.300, "valid second slot after ID zero");

    frame = makeFrame({108, 0x64, 0x00, 7, 0xE8, 0x03, 0, 0});
    expect(decodeAtMillidegreeStep(frame, readings), "out-of-range slot does not invalidate frame");
    expect(readings.size() == 1, "ID 108 is ignored independently");
    expectReading(readings, 0, "7", 1.000, "valid second slot after ID 108");

    frame = makeFrame({8, 0x64, 0x00, 255, 0x64, 0x80, 0, 0});
    expect(decodeAtMillidegreeStep(frame, readings), "ID 255 does not invalidate its peer");
    expect(readings.size() == 1, "ID 255 is ignored");
    expectReading(readings, 0, "8", 0.100, "valid first slot before ID 255");
}

// Verifies that decoded readings update only sensor IDs present in the state.
void testSensorStateIntegration() {
    SensorState state({{"1", {}, -1}, {"2", {}, -1}, {"5", {}, -1},
                       {"6", {}, -1}, {"7", {}, -1}, {"9", {}, -1},
                       {"23", {}, -1}, {"107", {}, -1}});
    std::vector<SensorReading> readings;
    const CanFrame pair = makeFrame({1, 0x64, 0x00, 2, 0x64, 0x80, 0, 0});
    expect(decodeAtMillidegreeStep(pair, readings), "two-sensor state frame is accepted");
    for (const SensorReading& reading : readings) {
        expect(state.updateTemperature(reading.sensorId, reading.temperatureC),
               "decoded CSV sensor is updated");
    }
    const auto snapshot = state.snapshot();
    expect(snapshot[0].hasCurrentReading, "sensor 1 receives a current reading");
    expectNear(snapshot[0].temperature, 0.100, "sensor 1 state temperature");
    expect(snapshot[1].hasCurrentReading, "sensor 2 receives a current reading");
    expectNear(snapshot[1].temperature, -0.100, "sensor 2 state temperature");

    SensorState partialCsv({{"1", {}, -1}});
    const CanFrame missingCsvSensor =
        makeFrame({42, 0x64, 0x00, 0, 0, 0, 0, 0});
    expect(decodeAtMillidegreeStep(missingCsvSensor, readings),
           "frame with valid but absent CSV ID is decoded");
    expect(readings.size() == 1 && readings[0].sensorId == "42",
           "valid protocol ID is passed directly as sensor ID");
    expect(!partialCsv.updateTemperature(readings[0].sensorId,
                                         readings[0].temperatureC),
           "sensor absent from CSV is ignored by SensorState");
    expect(!partialCsv.snapshot()[0].hasCurrentReading,
           "missing CSV sensor does not update another sensor");
}

// Verifies that malformed CAN frames are rejected before producing readings.
void testInvalidFrames() {
    std::vector<SensorReading> readings{{"stale", 12.0}};
    CanFrame frame = makeFrame({1, 0, 0, 2, 0, 0, 0, 0});

    frame.length = 7;
    expect(!decodeCanFrame(frame, readings), "DLC other than 8 is rejected");
    expect(readings.empty(), "rejected DLC clears previous readings");

    frame = makeFrame({1, 0, 0, 2, 0, 0, 0, 0});
    frame.id = 0x3C1;
    expect(!decodeCanFrame(frame, readings), "wrong CAN ID is rejected");

    frame.id = kTemperatureCanId;
    frame.isExtended = true;
    expect(!decodeCanFrame(frame, readings), "extended CAN ID is rejected");

    frame.isExtended = false;
    frame.isRemote = true;
    expect(!decodeCanFrame(frame, readings), "RTR frame is rejected");

    frame.isRemote = false;
    frame.isError = true;
    expect(!decodeCanFrame(frame, readings), "error frame is rejected");

    frame.isError = false;
    frame.isFd = true;
    expect(!decodeCanFrame(frame, readings), "CAN FD frame is rejected");
}

// Verifies group bounds that protect the visualizer's fixed-size arrays.
void testSensorGroupValidation() {
    bool rejected = false;
    try {
        SensorState invalid({{"1", {}, 35}});
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "sensor group above the UI range is rejected safely");

    rejected = false;
    try {
        SensorState invalid({{"1", {}, -2}});
        static_cast<void>(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "sensor group below the ungrouped value is rejected safely");
}

}  // namespace

// Runs all CAN protocol and sensor state checks.
int main() {
    testTemperatureDecoding();
    testProtocolFrames();
    testSensorStateIntegration();
    testInvalidFrames();
    testSensorGroupValidation();

    if (failures != 0) {
        std::cerr << failures << " CAN protocol test(s) failed.\n";
        return 1;
    }
    std::cout << "All CAN protocol tests passed.\n";
    return 0;
}
