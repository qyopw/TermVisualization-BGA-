#pragma once

#include "can_protocol.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class CanReceiver {
public:
    using ReadingHandler = std::function<void(const SensorReading&)>;

    // Creates a stopped receiver.
    CanReceiver() = default;
    // Prevents copying the worker thread and its owned socket descriptor.
    CanReceiver(const CanReceiver&) = delete;
    CanReceiver& operator=(const CanReceiver&) = delete;
    // Stops the worker and releases its SocketCAN descriptor.
    ~CanReceiver();

    // Opens and filters the interface, then starts the receive worker.
    bool start(const std::string& interfaceName,
               CanByteOrder byteOrder, ReadingHandler onReading,
               std::string& error);
    // Requests worker shutdown and joins it before closing the socket.
    void stop() noexcept;

private:
    // Polls SocketCAN and forwards each decoded reading to the handler.
    void receiveLoop(ReadingHandler onReading, CanByteOrder byteOrder) noexcept;

    std::atomic<bool> stopRequested_{false};
    std::thread worker_;
    int socketFd_ = -1;
};
