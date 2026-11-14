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

    Vec3 operator-(const Vec3& other) const {
        return Vec3{x - other.x, y - other.y, z - other.z};
    }
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
    explicit SensorState(std::vector<Sensor> sensors);

    bool updateTemperature(const std::string& sensorId, double temperature,
                          std::chrono::steady_clock::time_point updatedAt =
                              std::chrono::steady_clock::now());
    std::vector<Sensor> snapshot() const;
    std::size_t size() const noexcept;

private:
    mutable std::mutex mutex_;
    std::vector<Sensor> sensors_;
    std::unordered_map<std::string, std::size_t> sensorIndexById_;
};
