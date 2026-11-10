#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include "../assist/actions.hpp"
#include "../physics/hitboxes.hpp"
#include "../replay/macro.hpp"
#include "../render/renderer.hpp"
#include "../settings/settings.hpp"
#include "stats.hpp"
#include "../physics/step_patch.hpp"

using namespace geode::prelude;

namespace {

constexpr int MAX_STEPS_PER_FRAME = 1000;

bool g_skipDraw = false;
bool g_postUpdateRan = false;
bool g_announced = false;
bool g_deathShown = false;

bool g_planActive = false;
int g_planCount = 1;
float g_planTick = 1.f / 240.f;
uint32_t g_frameSteps = 0;

bool wantsBypass(const vbot::Config& cfg) {
    return cfg.tpsBypass && !cfg.tpsUnavailable && cfg.tps > vbot::NORMAL_TPS;
}

bool patchAvailable(bool wanted) {
    if (!wanted) return false;
    if (vbot::stepPatch::PLATFORM && vbot::stepPatch::prepare()) return true;
    auto& cfg = vbot::Config::get();
    cfg.tpsUnavailable = true;
    std::string why = vbot::stepPatch::PLATFORM ? vbot::stepPatch::g_why : "it only works on Android 64-bit and Windows";
    std::string text = vbot::stepPatch::g_whyIsFullMessage ? why : "Vbot: TPS bypass unavailable, " + why;
    Notification::create(text, NotificationIcon::Warning, 4.f)->show();
    return false;
}

void drawGameHitboxes(PlayLayer* pl) {
    auto& stats = vbot::Stats::get();
    if (!pl->m_objectLayer) return;

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
    node->m_bUseArea = false;
    pl->updateDebugDraw();
    stats.hitboxVertices = static_cast<int>(node->m_nBufferCount);
}

}

class $modify(VbTpsLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto& cfg = vbot::Config::get();
        auto* pl = PlayLayer::get();
        bool inLevel = pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this);

        if (!inLevel) {
            vbot::stepPatch::setEnabled(false);
            GJBaseGameLayer::update(dt);
            return;
        }

        g_postUpdateRan = false;
        auto& stats = vbot::Stats::get();
        bool patchAvail = patchAvailable(wantsBypass(cfg));
        bool patchMode = patchAvail && !pl->m_playerDied;
        vbot::stepPatch::setEnabled(patchMode);

        if (!patchAvail) {
            stats.tpsMethod = "";
            GJBaseGameLayer::update(dt);
            vbot::Stats::get().tick(dt, 1);
            if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
            return;
        }

        stats.tpsMethod = "step patch";

        if (!patchMode) {
            GJBaseGameLayer::update(dt);
            if (cfg.hitboxes && !g_postUpdateRan) drawGameHitboxes(pl);
            return;
        }

        float warp = m_gameState.m_timeWarp;
        if (!std::isfinite(warp) || warp <= 0.f) warp = 1.f;
        warp = std::min(warp, 1.f);
        double tick = (1.0 / static_cast<double>(cfg.tps)) * static_cast<double>(warp);

        double pending = static_cast<double>(dt) + m_extraDelta;
        if (!vbot::Renderer::get().rendering()) {
            double cap = static_cast<double>(warp) / 30.0 * std::max(1.0, static_cast<double>(cfg.effectiveSpeed()));
            pending = std::min(pending, cap);
        }
        int count = static_cast<int>(std::floor(pending / tick + 0.01));
        count = std::clamp(count, 0, MAX_STEPS_PER_FRAME);
        double left = pending - static_cast<double>(count) * tick;
        if (left < 0.0 || left >= tick) left = 0.0;
        m_extraDelta = left;

        g_frameSteps += static_cast<uint32_t>(count);
        if (count == 0) return;

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
            return static_cast<double>(g_planTick) * static_cast<double>(g_planCount);
        }
        return GJBaseGameLayer::getModifiedDelta(dt);
    }
};

class $modify(VbScheduler, CCScheduler) {
    void update(float rawDt) {
        auto& cfg = vbot::Config::get();
        auto& stats = vbot::Stats::get();
        auto* pl = PlayLayer::get();

        float dt = rawDt;
        if (pl && cfg.effectiveSpeed() != 1.0f) {
            auto* scene = CCDirector::get()->getRunningScene();
            if (!(scene && scene->getChildByType<PauseLayer>(0))) dt = rawDt * cfg.effectiveSpeed();
        }

        bool patchMode = pl && patchAvailable(wantsBypass(cfg));

        auto& renderer = vbot::Renderer::get();
        if (pl && renderer.rendering()) {
            auto* scene = CCDirector::get()->getRunningScene();
            bool paused = scene && scene->getChildByType<PauseLayer>(0);
            if (!paused) {
                int tps = cfg.effectiveTps();
                int ticks = renderer.ticksForNextFrame(tps);
                float tickDt = 1.f / static_cast<float>(tps);
                if (patchMode) {
                    if (ticks > 0) CCScheduler::update(static_cast<float>(ticks) * tickDt);
                } else {
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

        if (!cfg.tpsBypass || !pl) g_announced = false;
        g_frameSteps = 0;
        CCScheduler::update(dt);
        if (patchMode) {
            stats.tick(rawDt, g_frameSteps);
            g_frameSteps = 0;
        }
    }
};

class $modify(VbDebugDraw, GJBaseGameLayer) {
    void updateDebugDraw() {
        auto* pl = PlayLayer::get();
        bool inLevel = pl && static_cast<GJBaseGameLayer*>(pl) == static_cast<GJBaseGameLayer*>(this);
        if (!inLevel || !(vbot::Config::get().hitboxes || g_deathShown) || !m_debugDrawNode) {
            GJBaseGameLayer::updateDebugDraw();
            return;
        }
        vbot::hb::draw(this, m_debugDrawNode);
    }
};

class $modify(VbCheatPlayLayer, PlayLayer) {
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (vbot::Config::get().noclip && object != m_anticheatSpike) return;
        vbot::Renderer::get().notePlayerDied();
        PlayLayer::destroyPlayer(player, object);
        auto& cfg = vbot::Config::get();
        if (cfg.shbod && !cfg.hitboxes) {
            g_deathShown = true;
            drawGameHitboxes(this);
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (!g_deathShown) return;
        g_deathShown = false;
        if (vbot::Config::get().hitboxes) return;
        updateDebugDrawSettings();
        if (!m_isDebugDrawEnabled && m_debugDrawNode) m_debugDrawNode->clear();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        g_postUpdateRan = true;
        if (g_skipDraw || !vbot::Config::get().hitboxes) return;
        drawGameHitboxes(this);
    }

    void onQuit() {
        vbot::Renderer::get().cancel("you left the level");
        PlayLayer::onQuit();
    }

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
