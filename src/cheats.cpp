// cheats.cpp - noclip, hitboxes, TPS bypass and speed hack.
//
// Noclip and TPS changes are cheats: while either is on, finishing a level does not count
// (the run is treated like a test run, so nothing is saved).
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include "renderer.hpp"
#include "settings.hpp"
#include "stats.hpp"
#include "step_patch.hpp"

using namespace geode::prelude;

namespace {

// A lag spike can hand us a huge frame time; never run more than this many steps for one frame.
constexpr int MAX_STEPS_PER_FRAME = 1000;

// While rendering at the normal 240 TPS, only the last tick of a video frame needs to draw hitboxes.
bool g_skipDraw = false;
// Set when the game's own postUpdate ran during the current update (so we don't draw twice).
bool g_postUpdateRan = false;
bool g_announced = false;       // the "TPS bypass on" toast was shown

// Step patch: what the current update() was told to run (see VbTpsLayer).
bool g_planActive = false;
int g_planCount = 1;
float g_planTick = 1.f / 240.f;
uint32_t g_frameSteps = 0;      // steps the patch ran this frame, reported to the Cheats tab

// True when the TPS bypass is switched on, above 240, and not known to be unavailable.
bool wantsBypass(const vbot::Config& cfg) {
    return cfg.tpsBypass && !cfg.tpsUnavailable && cfg.tps > vbot::NORMAL_TPS;
}

// True if the TPS bypass can run. If the step patch can't be used the bypass is marked unavailable
// (it then counts as off everywhere) and the player is told why.
bool patchAvailable(bool wanted) {
    if (!wanted) return false;
    if (vbot::stepPatch::PLATFORM && vbot::stepPatch::prepare()) return true;
    auto& cfg = vbot::Config::get();
    cfg.tpsUnavailable = true;
    std::string why = vbot::stepPatch::PLATFORM ? vbot::stepPatch::g_why : "it only works on Android 64-bit and Windows";
    Notification::create("Vbot: TPS bypass unavailable, " + why, NotificationIcon::Warning, 4.f)->show();
    return false;
}

// Draws the game's own hitboxes, whatever the mode. The game normally only does this in
// practice mode (PlayLayer::shouldDebugDraw is "debug draw on AND practice mode"), so we turn
// the drawing on and ask for it directly.
void drawGameHitboxes(PlayLayer* pl) {
    auto& stats = vbot::Stats::get();
    if (!pl->m_objectLayer) return;

    // The draw node should already exist; make one if the game never did.
    if (!pl->m_debugDrawNode) {
        pl->m_debugDrawNode = CCDrawNode::create();
        pl->m_objectLayer->addChild(pl->m_debugDrawNode, 1000);
        stats.hitboxNodeCreated = true;
    }
    if (!pl->m_debugDrawPoints) {
        pl->m_debugDrawPoints = new std::array<CCPoint, 400>();
    }

    auto* node = pl->m_debugDrawNode;
    pl->m_isDebugDrawEnabled = true;
    node->setVisible(true);
    node->m_bUseArea = false; // the game's draw-area culling can hide everything outside practice mode
    pl->updateDebugDraw();
    stats.hitboxVertices = static_cast<int>(node->m_nBufferCount);
}

} // namespace

// --- TPS bypass --------------------------------------------------------------------------------
//
// How the game steps physics: update(dt) asks getModifiedDelta(dt) for a time, works out a step
// count from it (about time * 240) and splits the time into that many equal steps.
//
// Vbot has one method for the TPS bypass, the STEP PATCH (see step_patch.hpp): the count
// calculation is replaced so it reads a number from us. Each frame we decide how many steps of
// exactly 1/TPS to run and tell the game that number, and getModifiedDelta returns
// (1/TPS x timeWarp) x count so the game's own division gives back that exact step. The game
// runs all the steps itself, in one update(), the way it runs its 4 steps per frame at 60 FPS.
// Checkpoints, inputs and hitboxes keep working because it is the game's own loop.
//
// If the patch can't be used (not Android 64-bit or Windows, another mod patching the same
// place, or a game version other than 2.2081) the
// bypass stays off and says so; it never falls back to anything else.
//
// A higher TPS makes physics more precise. It does NOT make the game run faster, so the level
// looks the same; the Cheats tab shows the measured steps per second to prove it is working.

class $modify(VbTpsLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto& cfg = vbot::Config::get();
        auto* pl = PlayLayer::get();
        bool inLevel = pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this);

        if (!inLevel) {
            vbot::stepPatch::setEnabled(false); // the editor and everything else runs the game's own math
            GJBaseGameLayer::update(dt);
            return;
        }

        g_postUpdateRan = false;
        auto& stats = vbot::Stats::get();
        bool patchAvail = patchAvailable(wantsBypass(cfg));
        // After a death the game must run its own code, or the auto-retry freezes.
        bool patchMode = patchAvail && !pl->m_playerDied;
        vbot::stepPatch::setEnabled(patchMode);

        if (!patchAvail) {
            // Bypass off (or unavailable): the game's own code, untouched.
            stats.tpsMethod = "";
            GJBaseGameLayer::update(dt);
            vbot::Stats::get().tick(dt, 1);
            if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
            return;
        }

        stats.tpsMethod = "step patch";

        if (!patchMode) {
            // A frame after a death: the game runs it as it is.
            GJBaseGameLayer::update(dt);
            if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
            return;
        }

        // Time warp: the game's own step length is 1/240 x warp, and warp above 1 is ignored.
        float warp = m_gameState.m_timeWarp;
        if (!std::isfinite(warp) || warp <= 0.f) warp = 1.f;
        warp = std::min(warp, 1.f);
        double tick = (1.0 / static_cast<double>(cfg.tps)) * static_cast<double>(warp);

        // Time we owe: this frame's time plus what was left over last frame.
        double pending = static_cast<double>(dt) + m_extraDelta;
        if (!vbot::Renderer::get().rendering()) {
            // A slow frame must not snowball: never take more than 1/30 s of real time per frame
            // (scaled by the speed hack). If the device can't keep up, the game just runs slower.
            double cap = static_cast<double>(warp) / 30.0 * std::max(1.0, static_cast<double>(cfg.effectiveSpeed()));
            pending = std::min(pending, cap);
        }
        int count = static_cast<int>(std::floor(pending / tick + 0.01)); // +0.01: float error must not lose a step
        count = std::clamp(count, 0, MAX_STEPS_PER_FRAME);
        double left = pending - static_cast<double>(count) * tick;
        if (left < 0.0 || left >= tick) left = 0.0;
        m_extraDelta = left;

        g_frameSteps += static_cast<uint32_t>(count);
        if (count == 0) return; // not enough time for a step yet (frame rate above the TPS)

        if (!g_announced) {
            g_announced = true;
            Notification::create(fmt::format("Vbot: TPS bypass on, {} TPS", cfg.tps),
                                 NotificationIcon::Info, 1.5f)->show();
        }

        g_planTick = static_cast<float>(tick);
        g_planCount = count;
        g_planActive = true;
        vbot::stepPatch::g_count = static_cast<uint32_t>(count);
        GJBaseGameLayer::update(dt);
        g_planActive = false;

        if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
    }

    double getModifiedDelta(float dt) {
        if (g_planActive) {
            // The patched code runs `g_planCount` steps; this makes each one exactly g_planTick long.
            return static_cast<double>(g_planTick) * static_cast<double>(g_planCount);
        }
        return GJBaseGameLayer::getModifiedDelta(dt);
    }
};

// --- the scheduler (CCScheduler) ---------------------------------------------------------------
//
// The scheduler is what calls update() on everything each frame. It does three jobs here:
//  - speed hack: scales the time the whole game gets,
//  - rendering: advances the game by exactly one video frame per call,
//  - reporting: tells the Cheats tab how many physics steps the TPS bypass ran.

class $modify(VbScheduler, CCScheduler) {
    void update(float rawDt) {
        auto& cfg = vbot::Config::get();
        auto& stats = vbot::Stats::get();
        auto* pl = PlayLayer::get();

        // Speed hack: scale the time the whole game gets. Not while the pause menu is open.
        float dt = rawDt;
        if (pl && cfg.effectiveSpeed() != 1.0f) {
            auto* scene = CCDirector::get()->getRunningScene();
            if (!(scene && scene->getChildByType<PauseLayer>(0))) dt = rawDt * cfg.effectiveSpeed();
        }

        bool patchMode = pl && patchAvailable(wantsBypass(cfg));

        // Rendering: the game advances by exactly one video frame per call, however long the frame
        // really takes. Everything below is skipped.
        auto& renderer = vbot::Renderer::get();
        if (pl && renderer.rendering()) {
            auto* scene = CCDirector::get()->getRunningScene();
            bool paused = scene && scene->getChildByType<PauseLayer>(0);
            if (!paused) {
                int tps = cfg.effectiveTps();
                int ticks = renderer.ticksForNextFrame(tps);
                float tickDt = 1.f / static_cast<float>(tps);
                if (patchMode) {
                    // One update() that holds all the steps of this video frame.
                    if (ticks > 0) CCScheduler::update(static_cast<float>(ticks) * tickDt);
                } else {
                    // Normal 240 TPS: one short tick per step.
                    for (int i = 0; i < ticks; i++) {
                        g_skipDraw = (i != ticks - 1);
                        CCScheduler::update(tickDt);
                    }
                    g_skipDraw = false;
                }
                if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
                renderer.captureFrame();
                return;
            }
        }

        // Only forget the toast when the bypass is off or you left the level, so restarting or pausing
        // doesn't show it again.
        if (!cfg.tpsBypass || !pl) g_announced = false;
        g_frameSteps = 0;
        CCScheduler::update(dt);
        if (patchMode) {
            // The step patch ran its steps inside update(); report them for the Cheats tab.
            stats.tick(rawDt, g_frameSteps);
            g_frameSteps = 0;
        }
    }
};

// --- noclip, hitboxes and safe completion (PlayLayer) ------------------------------------------

class $modify(VbCheatPlayLayer, PlayLayer) {
    // Noclip: ignore deaths. The anticheat spike is the one thing that still kills,
    // so the game's own cheat protection keeps working.
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (vbot::Config::get().noclip && object != m_anticheatSpike) return;
        vbot::Renderer::get().notePlayerDied();
        PlayLayer::destroyPlayer(player, object);
    }

    // Hitboxes: after the game's own per-frame work, draw them.
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        g_postUpdateRan = true;
        if (g_skipDraw || !vbot::Config::get().hitboxes) return;
        drawGameHitboxes(this);
    }

    // Leaving the level ends a render in progress.
    void onQuit() {
        vbot::Renderer::get().cancel("you left the level");
        PlayLayer::onQuit();
    }

    // Cheats on: treat the completion as a test run so it isn't saved.
    void levelComplete() {
        vbot::Renderer::get().noteLevelComplete();
        if (vbot::Config::get().cheatsActive()) {
            m_isTestMode = true;
            Notification::create("Vbot: cheats are on, this completion won't count",
                                 NotificationIcon::Warning)->show();
        }
        PlayLayer::levelComplete();
    }
};
