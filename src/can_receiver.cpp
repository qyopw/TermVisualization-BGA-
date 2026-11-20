#include "can_receiver.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <utility>
#include <vector>

#if BGA_HAS_SOCKETCAN
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// Joins the worker before the receiver object is destroyed.
CanReceiver::~CanReceiver() {
    stop();
}

// Opens the fixed protocol filter and launches the receiver worker.
bool CanReceiver::start(const std::string& interfaceName,
                        CanByteOrder byteOrder, ReadingHandler onReading,
                        std::string& error) {
    if (worker_.joinable() || socketFd_ >= 0) {
        error = "CAN receiver has already been started.";
        return false;
    }
    if (interfaceName.empty()) {
        error = "CAN interface name cannot be empty.";
        return false;
    }
#if BGA_HAS_SOCKETCAN
    socketFd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socketFd_ < 0) {
        error = "Cannot open SocketCAN RAW socket: " +
                std::string(std::strerror(errno));
        return false;
    }

    // Match only standard 0x3C0 data frames. Masking EFF, RTR, and ERR flags
    // excludes extended, remote, and error frames in the kernel filter.
    can_filter socketFilter{};
    socketFilter.can_id = kTemperatureCanId;
    socketFilter.can_mask =
        CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG;
    if (::setsockopt(socketFd_, SOL_CAN_RAW, CAN_RAW_FILTER, &socketFilter,
                     sizeof(socketFilter)) < 0) {
        error = "Cannot install SocketCAN ID filters: " +
                std::string(std::strerror(errno));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    const unsigned int interfaceIndex = ::if_nametoindex(interfaceName.c_str());
    if (interfaceIndex == 0) {
        error = "Cannot find CAN interface '" + interfaceName + "': " +
                std::string(std::strerror(errno));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(interfaceIndex);
    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0) {
        error = "Cannot bind SocketCAN socket to '" + interfaceName + "': " +
                std::string(std::strerror(errno));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    stopRequested_.store(false);
    try {
        worker_ = std::thread(&CanReceiver::receiveLoop, this,
                              std::move(onReading), byteOrder);
    } catch (const std::exception& exception) {
        error = "Cannot start CAN receiver thread: " +
                std::string(exception.what());
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }
    return true;
#else
    (void)byteOrder;
    (void)onReading;
    error = "SocketCAN is available only on Linux; CAN reception is disabled.";
    return false;
#endif
}

// Stops the polling loop, joins it, and closes the socket.
void CanReceiver::stop() noexcept {
    stopRequested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
#if BGA_HAS_SOCKETCAN
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
#endif
}

// Polls for classical CAN frames and dispatches decoded sensor readings.
void CanReceiver::receiveLoop(ReadingHandler onReading,
                              CanByteOrder byteOrder) noexcept {
#if BGA_HAS_SOCKETCAN
    pollfd descriptor{};
    descriptor.fd = socketFd_;
    descriptor.events = POLLIN;
    std::vector<SensorReading> readings;

    while (!stopRequested_.load()) {
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "CAN receive poll error: " << std::strerror(errno)
                      << '\n';
            break;
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            std::cerr << "CAN socket became invalid; stopping receiver.\n";
            break;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP)) != 0) {
            std::cerr << "CAN interface disconnected or reported an error; "
                         "stopping receiver.\n";
            break;
        }
        if ((descriptor.revents & POLLIN) == 0) {
            continue;
        }

        // CAN_RAW_FD_FRAMES is not enabled, so this socket receives Classical
        // CAN frames only.
        can_frame systemFrame{};
        const ssize_t bytesRead = ::read(socketFd_, &systemFrame,
                                         sizeof(systemFrame));
        if (bytesRead < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (stopRequested_.load()) {
                break;
            }
            std::cerr << "CAN frame read error: " << std::strerror(errno)
                      << '\n';
            break;
        }
        if (bytesRead == 0) {
            std::cerr << "CAN interface disconnected; stopping receiver.\n";
            break;
        }
        if (static_cast<std::size_t>(bytesRead) != sizeof(systemFrame)) {
            std::cerr << "Ignoring incomplete CAN frame.\n";
            continue;
        }
        const bool isExtended = (systemFrame.can_id & CAN_EFF_FLAG) != 0;
        const canid_t identifierMask = isExtended ? CAN_EFF_MASK : CAN_SFF_MASK;
        if (systemFrame.len > CAN_MAX_DLEN ||
            (systemFrame.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0 ||
            (systemFrame.can_id & ~(identifierMask | CAN_EFF_FLAG |
                                    CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0) {
            continue;
        }

        CanFrame frame;
        frame.id = systemFrame.can_id & identifierMask;
        frame.length = systemFrame.len;
        frame.isExtended = isExtended;
        for (std::size_t index = 0; index < frame.length; ++index) {
            frame.data[index] = systemFrame.data[index];
        }

        if (!decodeCanFrame(frame, readings, byteOrder)) {
            continue;
        }
        for (const SensorReading& reading : readings) {
            try {
                onReading(reading);
            } catch (const std::exception& exception) {
                std::cerr << "CAN reading handler error: " << exception.what()
                          << '\n';
            } catch (...) {
                std::cerr << "CAN reading handler failed with an unknown error.\n";
            }
        }
    }
#else
    (void)byteOrder;
    (void)onReading;
#endif
}
