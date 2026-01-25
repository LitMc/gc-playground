#pragma once
#include "joybus_pio_sm.hpp"
#include "pad_console_link.hpp"
#include "shared_console.hpp"
#include "shared_pad.hpp"

namespace ConvertGcInput {
class ConsoleClient {
  public:
    explicit ConsoleClient(JoybusPioSm::Config device_to_console_config, PadConsoleLink &link)
        : link_{link}, shared_pad_{link.shared_pad()}, shared_console_{link.shared_console()},
          device_to_console_(device_to_console_config, &ConvertGcInput::ConsoleClient::callback,
                             this) {};
    // コンソールからの応答を受信したときに呼ぶコールバック
    static std::size_t callback(void *user, const uint8_t *rx, std::size_t rx_len, uint8_t *tx,
                                std::size_t tx_max);

  private:
    PadConsoleLink &link_;
    SharedPad &shared_pad_;
    SharedConsole &shared_console_;
    JoybusPioSm device_to_console_;
};
} // namespace ConvertGcInput
