#pragma once
#include "../bot/vb.hpp"
#include <cstddef>
#include <cstdint>

namespace vbot {

enum class Mode { Idle, Recording, Replaying };

struct State {
    Mode mode = Mode::Idle;
    uint32_t frame = 0;
    size_t nextInput = 0;
    size_t nextSnap = 0;
    vb::Macro macro;

    vb::Snapshot step = [] { vb::Snapshot s; s.frame = 0xFFFFFFFFu; return s; }();
    bool forceSnap = false;

    static State& get() {
        static State s;
        return s;
    }
};

}
