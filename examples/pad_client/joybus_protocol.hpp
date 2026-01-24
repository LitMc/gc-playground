#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ConvertGcInput::Joybus {
enum class Command : uint8_t {
    Id = 0x00,
    Status = 0x40,
    Origin = 0x41,
    Recalibrate = 0x42,
    Reset = 0xFF
};

static inline constexpr bool is_valid_command(uint8_t cmd) {
    switch (cmd) {
    case static_cast<uint8_t>(Command::Id):
    case static_cast<uint8_t>(Command::Status):
    case static_cast<uint8_t>(Command::Origin):
    case static_cast<uint8_t>(Command::Recalibrate):
    case static_cast<uint8_t>(Command::Reset):
        return true;
    default:
        return false;
    }
}

enum class PollMode : uint8_t {
    Mode0 = 0x00,
    Mode1 = 0x01,
    Mode2 = 0x02,
    Mode3 = 0x03,
    Mode4 = 0x04,
    Default = 0x03,
};

enum class RumbleMode : uint8_t {
    Off = 0x00,
    On = 0x01,
    Brake = 0x02,
};

constexpr std::size_t kIdResponseSize = 3;
constexpr std::size_t kOriginResponseSize = 10;
constexpr std::size_t kStatusResponseSize = 8;
constexpr std::size_t kRecalibrateResponseSize = 10;
constexpr std::size_t kResetResponseSize = 3;

template <std::size_t N> struct Request {
    static_assert(N >= 1);
    std::array<uint8_t, N> tx;
    std::size_t expected_rx_size;
    constexpr std::span<const uint8_t> bytes() const { return tx; }
    constexpr Command command() const { return static_cast<Command>(tx[0]); }
};

inline constexpr Request<1> Id{{static_cast<uint8_t>(Command::Id)}, kIdResponseSize};

inline constexpr Request<1> Origin{{static_cast<uint8_t>(Command::Origin)}, kOriginResponseSize};

[[nodiscard]] constexpr Request<3> Status(PollMode poll_mode = PollMode::Default,
                                          RumbleMode rumble_mode = RumbleMode::Off) {
    return {{static_cast<uint8_t>(Command::Status), static_cast<uint8_t>(poll_mode),
             static_cast<uint8_t>(rumble_mode)},
            kStatusResponseSize};
};

inline constexpr Request<3> Recalibrate{{static_cast<uint8_t>(Command::Recalibrate), 0x00, 0x00},
                                        kRecalibrateResponseSize};

inline constexpr Request<1> Reset{{static_cast<uint8_t>(Command::Reset)}, kResetResponseSize};

} // namespace ConvertGcInput::Joybus
