#pragma once

#include "can_protocol.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class CanReceiver {
public:
    using ReadingHandler = std::function<void(const SensorReading&)>;

    CanReceiver() = default;
    CanReceiver(const CanReceiver&) = delete;
    CanReceiver& operator=(const CanReceiver&) = delete;
    ~CanReceiver();

    bool start(const std::string& interfaceName,
               const std::vector<std::uint32_t>& sensorCanIds,
               ReadingHandler onReading, std::string& error);
    void stop() noexcept;

private:
    void receiveLoop(ReadingHandler onReading) noexcept;

    std::atomic<bool> stopRequested_{false};
    std::thread worker_;
    int socketFd_ = -1;
};
