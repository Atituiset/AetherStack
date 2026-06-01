#ifndef AETHER_APP_APP_LAYER_H
#define AETHER_APP_APP_LAYER_H

#include <cstdint>
#include <functional>
#include <vector>

namespace app {

class AppLayer {
public:
    using SendCallback = std::function<void(const std::vector<uint8_t>&)>;

    void set_send_callback(SendCallback cb);

    void send_data(const std::vector<uint8_t>& data);
    void on_data_received(const std::vector<uint8_t>& data);

    uint32_t tx_count() const { return tx_count_; }
    uint32_t rx_count() const { return rx_count_; }
    const std::vector<uint8_t>& last_received() const { return last_rx_; }

private:
    SendCallback send_cb_;
    uint32_t tx_count_ = 0;
    uint32_t rx_count_ = 0;
    std::vector<uint8_t> last_rx_;
};

}

#endif
