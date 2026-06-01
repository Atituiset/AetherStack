#include "app/app_layer.h"
#include "common/logger.h"

namespace app {

void AppLayer::set_send_callback(SendCallback cb) { send_cb_ = std::move(cb); }

void AppLayer::send_data(const std::vector<uint8_t>& data) {
    tx_count_++;
    std::string hex;
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        hex += buf;
        if (i + 1 < data.size() && i + 1 < 32) hex += ":";
    }
    LOG_INFO("APP_DATA_TX", {{"seq", std::to_string(tx_count_)},
                              {"len", std::to_string(data.size())},
                              {"hex", hex}});
    if (send_cb_) send_cb_(data);
}

void AppLayer::on_data_received(const std::vector<uint8_t>& data) {
    rx_count_++;
    last_rx_ = data;
    std::string hex;
    for (size_t i = 0; i < data.size() && i < 32; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        hex += buf;
        if (i + 1 < data.size() && i + 1 < 32) hex += ":";
    }
    LOG_INFO("APP_DATA_RX", {{"seq", std::to_string(rx_count_)},
                              {"len", std::to_string(data.size())},
                              {"hex", hex}});
}

}
