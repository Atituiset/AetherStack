#include "app/u2u_fields.h"

namespace app {

// 读 src_imsi_len 字段（wire offset 10）。依赖调用方已校验缓冲区长度。
static uint8_t src_len_at(const uint8_t *data) {
    return data[10];
}

static uint8_t dst_len_at(const uint8_t *data) {
    return data[11 + src_len_at(data)];
}

static size_t imsi_region_end(const uint8_t *data) {
    return 12 + src_len_at(data) + dst_len_at(data);
}

size_t payload_offset(const uint8_t *data) {
    return imsi_region_end(data);
}

} // namespace app
