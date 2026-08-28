#include "app/u2u_media.h"

namespace app {

// 读 src_imsi_len 字段（wire offset 10）。依赖调用方已校验缓冲区长度。
static int msg_src_len(const uint8_t *data)   { return data[10]; }
static int voice_src_len(const uint8_t *data) { return data[10]; }
static int video_src_len(const uint8_t *data) { return data[10]; }

using MediaSrcLenFn = int (*)(const uint8_t *);
static const MediaSrcLenFn MEDIA_SRC_LEN[] = {
    msg_src_len,
    voice_src_len,
    video_src_len,
};

int media_src_len(MediaKind kind, const uint8_t *data) {
    return MEDIA_SRC_LEN[static_cast<int>(kind)](data);
}

} // namespace app
