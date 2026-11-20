#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Sensor {
    std::string id;
    Vec3 position;
    int group = -1;
    double temperature = 0.0;
    bool hasCurrentReading = false;
    std::chrono::steady_clock::time_point lastUpdated{};
};

class SensorState {
public:
    // Stores sensors and builds a unique ID-to-index lookup.
    explicit SensorState(std::vector<Sensor> sensors);

    // Updates one known sensor, returning false for invalid or unknown data.
    bool updateTemperature(const std::string& sensorId, double temperature,
                          std::chrono::steady_clock::time_point updatedAt =
                              std::chrono::steady_clock::now());
    // Returns a locked copy for rendering outside the state mutex.
    std::vector<Sensor> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<Sensor> sensors_;
    std::unordered_map<std::string, std::size_t> sensorIndexById_;
};
