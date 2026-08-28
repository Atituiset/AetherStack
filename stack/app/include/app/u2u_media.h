#ifndef AETHER_APP_U2U_MEDIA_H
#define AETHER_APP_U2U_MEDIA_H

#include "app/u2u.h"

namespace app {

// 按媒体类型分发读取 src_imsi_len。调用方须先校验缓冲区长度（入口契约）
int media_src_len(MediaKind kind, const uint8_t *data);

} // namespace app

#endif
