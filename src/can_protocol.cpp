#include "can_protocol.hpp"

#include <charconv>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <utility>

namespace {

bool parseSensorIndex(const std::string& sensorId, std::uint32_t& sensorIndex) {
    std::uint32_t parsed = 0;
    const char* const begin = sensorId.data();
    const char* const end = begin + sensorId.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (sensorId.empty() || result.ec != std::errc{} || result.ptr != end ||
        parsed == 0 || std::to_string(parsed) != sensorId) {
        return false;
    }

    sensorIndex = parsed;
    return true;
}

bool decimalToPackedId(std::uint32_t value, std::uint32_t& packed) {
    std::uint64_t result = 0;
    unsigned int shift = 0;
    do {
        result |= static_cast<std::uint64_t>(value % 10) << shift;
        value /= 10;
        shift += 4;
    } while (value != 0);

    if (result > 0x1FFFFFFF) {
        return false;
    }
    packed = static_cast<std::uint32_t>(result);
    return true;
}

std::uint32_t packedIdToDecimal(std::uint32_t packed) {
    std::uint64_t result = 0;
    std::uint64_t multiplier = 1;
    while (packed != 0) {
        const std::uint32_t digit = packed & 0xF;
        if (digit > 9) {
            return 0;
        }
        result += digit * multiplier;
        multiplier *= 10;
        packed >>= 4;
    }
    if (result > std::numeric_limits<std::uint32_t>::max()) {
        return 0;
    }
    return static_cast<std::uint32_t>(result);
}

}  // namespace

bool buildCanFiltersForSensors(const std::vector<std::string>& sensorIds,
                               std::vector<CanFilterId>& filters,
                               std::string& error) {
    filters.clear();
    std::set<std::pair<bool, std::uint32_t>> uniqueFilters;
    for (const std::string& sensorId : sensorIds) {
        std::uint32_t sensorIndex = 0;
        if (!parseSensorIndex(sensorId, sensorIndex)) {
            error = "Sensor ID '" + sensorId +
                    "' must be a canonical positive decimal number for CAN.";
            return false;
        }

        const std::uint32_t groupId = (sensorIndex - 1) / 8 + 1;
        bool hasGroupFilter = false;
        if (groupId <= 0x7FF) {
            uniqueFilters.emplace(false, groupId);
            hasGroupFilter = true;
        }

        std::uint32_t packedId = 0;
        if (decimalToPackedId(groupId, packedId)) {
            uniqueFilters.emplace(true, packedId);
            hasGroupFilter = true;
        }
        if (!hasGroupFilter) {
            error = "Sensor group ID is outside the supported CAN-ID range.";
            return false;
        }
    }

    filters.reserve(uniqueFilters.size());
    for (const auto& [isExtended, id] : uniqueFilters) {
        filters.push_back({id, isExtended});
    }
    if (filters.empty()) {
        error = "No CAN message filters could be created for the sensor IDs.";
        return false;
    }
    return true;
}

bool decodeCanFrame(const CanFrame& frame,
                    std::vector<SensorReading>& readings) {
    const std::uint32_t maximumId = frame.isExtended ? 0x1FFFFFFF : 0x7FF;
    if (frame.id > maximumId || frame.length != 8) {
        return false;
    }

    std::uint32_t groupId = frame.id;
    if (frame.isExtended) {
        const std::uint32_t packedDecimalGroup = packedIdToDecimal(frame.id);
        if (packedDecimalGroup != 0) {
            groupId = packedDecimalGroup;
        }
    }
    if (groupId == 0) {
        return false;
    }

    const std::uint64_t firstSensor =
        (static_cast<std::uint64_t>(groupId) - 1) * 8 + 1;
    if (firstSensor + 7 > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    readings.clear();
    readings.reserve(8);
    for (std::size_t index = 0; index < frame.data.size(); ++index) {
        const std::uint64_t sensorIndex = firstSensor + index;
        readings.push_back({std::to_string(sensorIndex),
                            static_cast<double>(frame.data[index])});
    }
    return true;
}
