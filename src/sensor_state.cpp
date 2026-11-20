#include "sensor_state.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

// Validates sensor metadata and builds the direct sensor ID lookup.
SensorState::SensorState(std::vector<Sensor> sensors)
    : sensors_(std::move(sensors)) {
    sensorIndexById_.reserve(sensors_.size());
    for (std::size_t index = 0; index < sensors_.size(); ++index) {
        if (sensors_[index].id.empty()) {
            throw std::invalid_argument("Sensor IDs cannot be empty.");
        }
        if (sensors_[index].group < -1 || sensors_[index].group > 34) {
            throw std::invalid_argument(
                "Sensor group must be -1 or an integer from 0 to 34.");
        }
        if (!sensorIndexById_.emplace(sensors_[index].id, index).second) {
            throw std::invalid_argument("Duplicate sensor ID: " + sensors_[index].id);
        }
    }
}

// Updates one known sensor while protecting shared state with the mutex.
bool SensorState::updateTemperature(
    const std::string& sensorId, double temperature,
    std::chrono::steady_clock::time_point updatedAt) {
    if (!std::isfinite(temperature)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sensorIndexById_.find(sensorId);
    if (found == sensorIndexById_.end()) {
        return false;
    }

    Sensor& sensor = sensors_[found->second];
    sensor.temperature = temperature;
    sensor.hasCurrentReading = true;
    sensor.lastUpdated = updatedAt;
    return true;
}

// Copies the current sensor values for a consistent render pass.
std::vector<Sensor> SensorState::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sensors_;
}
