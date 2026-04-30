// core/src/ipc/StateModel.cpp

#include "ipc/StateModel.h"
#include <string>

namespace spe::ipc {

void StateModel::reset() noexcept {
    for (auto& o : objects_) o = ObjectEntry{};
    default_algo_        = Algorithm::VBAP;
    osc_reordered_drops_ = 0;
    burst_alerts_        = 0;
    reorder_window_      = ReorderWindow{};
}

void StateModel::checkBurst(uint64_t now_ms) noexcept {
    int n = reorder_window_.countInWindow(now_ms);
    if (n >= ReorderWindow::BURST_THRESH) {
        ++burst_alerts_;
        if (warning_cb_) {
            Reply r;
            r.tag     = ReplyTag::Warning;
            r.seq     = 0;
            r.message = "osc_reorder_burst";
            warning_cb_(r);
        }
    }
}

bool StateModel::apply(const Command& cmd, uint64_t now_ms) noexcept {
    switch (cmd.tag) {
    case CommandTag::ObjMove: {
        auto& p = std::get<PayloadObjMove>(cmd.payload);
        if (p.obj_id >= STATE_MAX_OBJECTS) return false;
        auto& obj = objects_[p.obj_id];
        // Seq check: drop if this seq <= last_applied_seq (reorder/duplicate).
        if (obj.valid && cmd.seq <= obj.last_applied_seq) {
            ++osc_reordered_drops_;
            reorder_window_.push(now_ms);
            checkBurst(now_ms);
            return false;
        }
        obj.last_applied_seq = cmd.seq;
        obj.az_rad = p.az_rad;
        obj.el_rad = p.el_rad;
        obj.dist_m = p.dist_m;
        obj.valid  = true;
        return true;
    }
    case CommandTag::ObjGain: {
        auto& p = std::get<PayloadObjGain>(cmd.payload);
        if (p.obj_id >= STATE_MAX_OBJECTS) return false;
        auto& obj = objects_[p.obj_id];
        if (obj.valid && cmd.seq <= obj.last_applied_seq) {
            ++osc_reordered_drops_;
            reorder_window_.push(now_ms);
            checkBurst(now_ms);
            return false;
        }
        obj.last_applied_seq = cmd.seq;
        obj.gain  = p.gain;
        obj.valid = true;
        return true;
    }
    case CommandTag::ObjActive: {
        auto& p = std::get<PayloadObjActive>(cmd.payload);
        if (p.obj_id >= STATE_MAX_OBJECTS) return false;
        auto& obj = objects_[p.obj_id];
        if (obj.valid && cmd.seq <= obj.last_applied_seq) {
            ++osc_reordered_drops_;
            reorder_window_.push(now_ms);
            checkBurst(now_ms);
            return false;
        }
        obj.last_applied_seq = cmd.seq;
        obj.active = p.active;
        obj.valid  = true;
        return true;
    }
    case CommandTag::ObjAlgo: {
        auto& p = std::get<PayloadObjAlgo>(cmd.payload);
        if (p.obj_id >= STATE_MAX_OBJECTS) return false;
        auto& obj = objects_[p.obj_id];
        if (obj.valid && cmd.seq <= obj.last_applied_seq) {
            ++osc_reordered_drops_;
            reorder_window_.push(now_ms);
            checkBurst(now_ms);
            return false;
        }
        obj.last_applied_seq = cmd.seq;
        obj.algo  = p.algo;
        obj.valid = true;
        return true;
    }
    case CommandTag::SysAlgoSwap: {
        auto& p = std::get<PayloadSysAlgoSwap>(cmd.payload);
        default_algo_ = p.algo;
        return true;
    }
    case CommandTag::SysReset: {
        reset();
        return true;
    }
    default:
        // SysHandshake, HbPing/Pong, Unknown — not mutating object state.
        return false;
    }
}

} // namespace spe::ipc
