// ---- TRIAL SEED: 表驱动分发 + 深处解引用（mode-fp-tbl 对比），勿合并 ----
// 本文件内**没有任何判空**：契约约定在调用方入口判空。
#include "nas/trial_fp.h"

namespace nas::trial {

// 第 5 层（最深处）：解引用 req->buf——依赖入口契约
static int decode_header(const FpReq *req) { return req->buf[0]; }

// 第 2~4 层：纯转发
static int reassembly(const FpReq *req) { return decode_header(req); }
static int bearer_rx(const FpReq *req) { return reassembly(req); }

// 分发表：msg_id → handler（navmap 应提取此表）
using fp_handler_t = int (*)(const FpReq *);
static const fp_handler_t FP_HANDLER_TBL[4] = {
    bearer_rx, bearer_rx, bearer_rx, bearer_rx,
};

int fp_engine_run(uint32_t msg_id, const FpReq *req) {
    if (msg_id >= 4) {
        return -2;
    }
    return FP_HANDLER_TBL[msg_id](req);
}

} // namespace nas::trial
