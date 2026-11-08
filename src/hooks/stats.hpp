#pragma once
#include <cstdint>

namespace vbot {

struct Stats {
    int stepsPerSec = 0;
    int updatesPerSec = 0;

    double lastStepDt = 0.0;
    const char* tpsMethod = "";
    bool bypassIgnored = false;

    int hitboxVertices = -1;
    bool hitboxNodeCreated = false;

    uint32_t procCounter = 0;
    uint32_t updateCounter = 0;
    double window = 0.0;

    static Stats& get() {
        static Stats s;
        return s;
    }

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

}
