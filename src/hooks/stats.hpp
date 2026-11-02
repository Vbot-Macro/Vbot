// stats.hpp - live numbers the hooks collect so the Cheats tab can show what is really happening.
// No Geode dependency, so it can be unit-tested anywhere.
#pragma once
#include <cstdint>

namespace vbot {

struct Stats {
    // Published once per second of play.
    int stepsPerSec = 0;    // physics steps the game ran (processCommands calls)
    int updatesPerSec = 0;  // times the game's update() ran (frames, or TPS-bypass steps)

    // Size of the last physics step the game ran, in seconds (0 = unknown).
    double lastStepDt = 0.0;
    // How the TPS bypass is changing the step rate right now: "step patch", "scheduler" or "" (off).
    const char* tpsMethod = "";
    // The game ignored our short steps (it still stepped at ~1/240), so the bypass switched itself off.
    bool bypassIgnored = false;

    // Hitboxes: how many vertices the game's debug drawing produced on the last draw.
    // -1 means hitboxes haven't been drawn yet.
    int hitboxVertices = -1;
    bool hitboxNodeCreated = false; // Vbot had to create the draw node itself

    // Counters for the current one-second window.
    uint32_t procCounter = 0;
    uint32_t updateCounter = 0;
    double window = 0.0;

    static Stats& get() {
        static Stats s;
        return s;
    }

    // Call once per real frame with the frame time and how many update() calls it contained.
    void tick(double dt, uint32_t updates) {
        window += dt;
        updateCounter += updates;
        if (window >= 1.0) {
            stepsPerSec = static_cast<int>(procCounter / window + 0.5);
            updatesPerSec = static_cast<int>(updateCounter / window + 0.5);
            procCounter = 0;
            updateCounter = 0;
            window = 0.0;
        }
    }
};

} // namespace vbot
