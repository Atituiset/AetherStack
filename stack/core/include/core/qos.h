#ifndef AETHER_CORE_QOS_H
#define AETHER_CORE_QOS_H

// M17: QoS-differentiated dedicated bearers (LTE QCI style).
//
// Each service class gets its own logical channel (MAC LCID), its own RLC
// entity (AM for sig/best-effort, UM for voice/video) and its own queue; a
// strict-priority scheduler with a best-effort min-share guard decides
// which bearer feeds the HARQ pipe next, on both the UE uplink and the BS
// downlink:
//
//   QCI5 sig  (SIP-lite call control)   — highest priority, LCID_APP_SIG
//   QCI1 voice (conversational)         — LCID_APP_VOICE
//   QCI2 video (conversational)         — LCID_APP_VIDEO
//   QCI9 best effort (msg + legacy loopback) — LCID_APP_DTCH (default bearer)
//
// Design notes / simplifications (see docs/m17_plan.md):
//  * Voice/video bearers use RLC UM (segmenting, M17): media tolerates
//    loss and hates delay, and UM keeps a lossy flood at a constant
//    offered rate instead of amplifying into STATUS/retransmission churn.
//    Signaling and best-effort keep the M16.1-hardened AM.
//  * PDCP ciphering keeps ONE sequence space per direction shared across
//    bearers (the nonce stays unique; per-bearer COUNT is not modelled).
//  * Bearer setup is implicit (first SDU of a class), not a NAS/RRC
//    bearer-activation procedure.

#include "app/u2u.h"
#include "mac/mac_pdu.h"
#include "rlc/rlc_am.h"
#include "rlc/rlc_um.h"
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace core {

enum class Qci : uint8_t { VOICE = 1, VIDEO = 2, SIG = 5, BEST_EFFORT = 9 };

inline Qci qci_of(app::MediaKind kind) {
    switch (kind) {
        case app::MediaKind::SIG: return Qci::SIG;
        case app::MediaKind::VOICE: return Qci::VOICE;
        case app::MediaKind::VIDEO: return Qci::VIDEO;
        case app::MediaKind::MSG: return Qci::BEST_EFFORT;
    }
    return Qci::BEST_EFFORT;
}

inline uint8_t lcid_of(Qci qci) {
    switch (qci) {
        case Qci::SIG: return mac::LCID_APP_SIG;
        case Qci::VOICE: return mac::LCID_APP_VOICE;
        case Qci::VIDEO: return mac::LCID_APP_VIDEO;
        case Qci::BEST_EFFORT: return mac::LCID_APP_DTCH;
    }
    return mac::LCID_APP_DTCH;
}

// AM STATUS LCID per bearer; voice/video run UM and never send STATUS
// (they map to the default LCID, unreachable in practice).
inline uint8_t status_lcid_of(Qci qci) {
    switch (qci) {
        case Qci::SIG: return mac::LCID_RLC_STATUS_SIG;
        case Qci::VOICE:
        case Qci::VIDEO:
        case Qci::BEST_EFFORT: return mac::LCID_RLC_STATUS;
    }
    return mac::LCID_RLC_STATUS;
}

// Data LCID -> bearer, or nullopt for non-app channels.
inline std::optional<Qci> qci_of_lcid(uint8_t lcid) {
    switch (lcid) {
        case mac::LCID_APP_SIG: return Qci::SIG;
        case mac::LCID_APP_VOICE: return Qci::VOICE;
        case mac::LCID_APP_VIDEO: return Qci::VIDEO;
        case mac::LCID_APP_DTCH: return Qci::BEST_EFFORT;
        default: return std::nullopt;
    }
}

inline std::optional<Qci> qci_of_status_lcid(uint8_t lcid) {
    switch (lcid) {
        case mac::LCID_RLC_STATUS_SIG: return Qci::SIG;
        case mac::LCID_RLC_STATUS: return Qci::BEST_EFFORT;
        default: return std::nullopt;
    }
}

// Contract string for QOS_BEARER_SETUP/TEARDOWN `kind` fields.
inline const char* bearer_kind_name(Qci qci) {
    switch (qci) {
        case Qci::SIG: return "sig";
        case Qci::VOICE: return "voice";
        case Qci::VIDEO: return "video";
        case Qci::BEST_EFFORT: return "be";
    }
    return "be";
}

// One queued PDU ready for MAC/HARQ; the LCID tag lets control traffic
// (RLC STATUS) share a bearer's queue with its data. `cipher` captures the
// security decision AT ENQUEUE TIME: the ATTACH_ACCEPT must go out in the
// clear even though security arms immediately after it is queued (the
// drain happens later).
struct AppPdu {
    uint8_t lcid = 0;
    bool cipher = false;
    std::vector<uint8_t> bytes;
};

// One bearer: its RLC entities plus the queue of wrapped RLC PDUs waiting
// for HARQ capacity. Signaling and best-effort run AM (reliable control);
// voice/video run UM (M17 — media tolerates loss, hates delay; a lossy
// flood keeps a constant offered rate instead of amplifying into ARQ
// churn the way AM does).
template <typename TxT, typename RxT>
struct Bearer {
    Bearer(Qci q, TxT t, RxT r) : qci(q), tx(std::move(t)), rx(std::move(r)) {}
    Qci qci;
    TxT tx;
    RxT rx;
    std::deque<AppPdu> queue;               // pdcp-wrapped RLC PDUs
    bool established = false;               // QOS_BEARER_SETUP logged
};
using AmBearer = Bearer<rlc::AmTx, rlc::AmRx>;
using UmBearer = Bearer<rlc::UmTx, rlc::UmRx>;

inline AmBearer make_am_bearer(Qci qci) {
    return AmBearer(qci, rlc::AmTx{rlc::am_node_bearer_config()},
                    rlc::AmRx{rlc::am_node_bearer_config()});
}
inline UmBearer make_um_bearer(Qci qci) {
    rlc::UmConfig cfg;
    cfg.t_reorder_ms = 100; // media: a late packet is a useless packet
    return UmBearer(qci, rlc::UmTx{cfg}, rlc::UmRx{cfg});
}

// Strict-priority bearer set: ctrl (RRC/NAS/RLC STATUS) > sig > voice >
// video > best-effort, with a min-share guard so best-effort cannot
// starve — whenever it has data, at least every 4th pick is served from
// it (25% floor under full load).
class BearerSet {
public:
    AmBearer& am_of(Qci qci) { // sig | best-effort only
        return qci == Qci::SIG ? sig_ : be_;
    }
    UmBearer& um_of(Qci qci) { // voice | video only
        return qci == Qci::VOICE ? voice_ : video_;
    }
    std::deque<AppPdu>& queue_of(Qci qci) {
        switch (qci) {
            case Qci::SIG: return sig_.queue;
            case Qci::VOICE: return voice_.queue;
            case Qci::VIDEO: return video_.queue;
            case Qci::BEST_EFFORT: return be_.queue;
        }
        return be_.queue;
    }
    bool& established_of(Qci qci) {
        switch (qci) {
            case Qci::SIG: return sig_.established;
            case Qci::VOICE: return voice_.established;
            case Qci::VIDEO: return video_.established;
            case Qci::BEST_EFFORT: return be_.established;
        }
        return be_.established;
    }
    bool established_of(Qci qci) const {
        return const_cast<BearerSet*>(this)->established_of(qci);
    }

    // Connection-control traffic (CCCH/NAS/RLC STATUS) bypasses QoS classes
    // and is always served first.
    std::deque<AppPdu>& ctrl() { return ctrl_; }

    bool empty() const {
        return ctrl_.empty() && sig_.queue.empty() && voice_.queue.empty() &&
               video_.queue.empty() && be_.queue.empty();
    }

    // Pop the next PDU to feed HARQ. Call only when !empty().
    AppPdu pop_next() {
        if (!ctrl_.empty()) {
            AppPdu p = std::move(ctrl_.front());
            ctrl_.pop_front();
            return p;
        }
        std::deque<AppPdu>& q = pick_queue();
        AppPdu p = std::move(q.front());
        q.pop_front();
        return p;
    }

    uint32_t since_be() const { return since_be_; } // test observability

private:
    // Strict priority with the BE min-share guard.
    std::deque<AppPdu>& pick_queue() {
        if (!be_.queue.empty() && since_be_ >= 3) {
            since_be_ = 0;
            return be_.queue;
        }
        for (std::deque<AppPdu>* q :
             {&sig_.queue, &voice_.queue, &video_.queue, &be_.queue}) {
            if (!q->empty()) {
                if (q != &be_.queue) ++since_be_;
                return *q;
            }
        }
        return be_.queue; // unreachable when !empty()
    }

    std::deque<AppPdu> ctrl_;
    AmBearer sig_{make_am_bearer(Qci::SIG)};
    UmBearer voice_{make_um_bearer(Qci::VOICE)};
    UmBearer video_{make_um_bearer(Qci::VIDEO)};
    AmBearer be_{make_am_bearer(Qci::BEST_EFFORT)};
    uint32_t since_be_ = 0;
};

}

#endif // AETHER_CORE_QOS_H
