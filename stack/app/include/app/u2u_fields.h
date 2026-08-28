#ifndef AETHER_APP_U2U_FIELDS_H
#define AETHER_APP_U2U_FIELDS_H

#include <cstddef>
#include <cstdint>

namespace app {

// U2U wire 头字段提取：调用方须先校验缓冲区长度（入口契约）
size_t payload_offset(const uint8_t *data);

} // namespace app

#endif
