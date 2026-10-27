// macro.hpp - shared recorder/replayer state for Vbot.
#pragma once
#include "vb.hpp"
#include <cstddef>
#include <cstdint>

namespace vbot {

enum class Mode { Idle, Recording, Replaying };

struct State {
    Mode mode = Mode::Idle;
    uint32_t frame = 0;     // physics step counter, reset every time the level restarts
    size_t nextInput = 0;   // replay cursor into macro.inputs
    size_t nextSnap = 0;    // replay cursor into macro.snaps
    vb::Macro macro;        // the macro being recorded or replayed

    // Recording: the state of the players at the start of the current physics step (frame
    // 0xFFFFFFFF = none yet), and a request to take a snapshot on the next step.
    vb::Snapshot step = [] { vb::Snapshot s; s.frame = 0xFFFFFFFFu; return s; }();
    bool forceSnap = false;

    static State& get() {
        static State s;
        return s;
    }
};

} // namespace vbot
