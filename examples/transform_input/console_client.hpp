#pragma once
#include "joybus_pio_sm.hpp"
#include "pad_console_link.hpp"
#include "shared_console.hpp"
#include "shared_pad_hub.hpp"

namespace ConvertGcInput {
class ConsoleClient {
  public:
    explicit ConsoleClient(JoybusPioSm::Config device_to_console_config, PadConsoleLink &link)
        : link_{link}, shared_pad_hub_{link.shared_pad_hub()},
          shared_console_{link.shared_console()},
          device_to_console_(device_to_console_config, &ConvertGcInput::ConsoleClient::callback,
                             this) {};
    // コンソールからの応答を受信したときに呼ぶコールバック
    static std::size_t callback(void *user, const uint8_t *rx, std::size_t rx_len, uint8_t *tx,
                                std::size_t tx_max);

    static std::size_t write_tx(const JoybusReply &reply, uint8_t *tx, std::size_t tx_max);

  private:
    PadConsoleLink &link_;
    SharedPadHub &shared_pad_hub_;
    SharedConsole &shared_console_;
    JoybusPioSm device_to_console_;
};
} // namespace ConvertGcInput
