// ---- TRIAL SEED 头文件（mode-fp-tbl 对比），勿合并 ----
#pragma once
#include <cstdint>

namespace nas::trial {

struct FpReq {
    const char *buf;
    uint32_t len;
};

// 引擎入口（经函数指针表分发）
int fp_engine_run(uint32_t msg_id, const FpReq *req);

} // namespace nas::trial
