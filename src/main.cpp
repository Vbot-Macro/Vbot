#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "assist/actions.hpp"
#include "replay/macro.hpp"
#include "render/renderer.hpp"
#include "settings/settings.hpp"
#include "hooks/stats.hpp"

using namespace geode::prelude;

namespace {

constexpr int SNAPS_PER_SECOND = 8;

vb::PlayerState readState(PlayerObject* p) {
    return {p->getPositionX(), p->getPositionY(), static_cast<float>(p->m_yVelocity)};
}

vb::Snapshot captureSnapshot(GJBaseGameLayer* gl, uint32_t frame) {
    vb::Snapshot sn;
    sn.frame = frame;
    if (gl->m_player1) {
        sn.has[0] = true;
        sn.p[0] = readState(gl->m_player1);
    }
    if (gl->m_player2 && gl->m_gameState.m_isDualMode) {
        sn.has[1] = true;
        sn.p[1] = readState(gl->m_player2);
    }
    return sn;
}

void keepSnapshot(vbot::State& s, vb::Snapshot const& sn) {
    auto& snaps = s.macro.snaps;
    if (!snaps.empty() && snaps.back().frame >= sn.frame) return;
    snaps.push_back(sn);
}

void applySnapshot(GJBaseGameLayer* gl, vb::Snapshot const& sn) {
    for (int i = 0; i < 2; i++) {
        if (!sn.has[i]) continue;
        if (i == 1 && !gl->m_gameState.m_isDualMode) continue;
        PlayerObject* p = i == 1 ? gl->m_player2 : gl->m_player1;
        if (!p) continue;
        auto const& st = sn.p[i];
        bool drifted = std::abs(p->getPositionX() - st.x) > 0.01f ||
                       std::abs(p->getPositionY() - st.y) > 0.01f;
        if (!drifted) continue;
        p->setPosition({st.x, st.y});
        if (!p->m_isDart) p->m_yVelocity = st.yVel;
    }
}

}

class $modify(VbBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& s = vbot::State::get();

        if (!PlayLayer::get()) {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }

        vbot::Stats::get().lastStepDt = dt;
        vbot::Stats::get().procCounter++;

        if (s.mode == vbot::Mode::Recording) {
            s.step = captureSnapshot(this, s.frame);
            uint32_t every = static_cast<uint32_t>(
                std::max(1, vbot::Config::get().effectiveTps() / SNAPS_PER_SECOND));
            if (s.forceSnap || s.frame % every == 0) {
                keepSnapshot(s, s.step);
                s.forceSnap = false;
            }
        }

        if (s.mode == vbot::Mode::Replaying) {
            auto const& snaps = s.macro.snaps;
            while (s.nextSnap < snaps.size() && snaps[s.nextSnap].frame <= s.frame) {
                auto const& sn = snaps[s.nextSnap++];
                if (sn.frame == s.frame) applySnapshot(this, sn);
            }

            auto const& inputs = s.macro.inputs;
            while (s.nextInput < inputs.size() && inputs[s.nextInput].frame <= s.frame) {
                auto const& in = inputs[s.nextInput++];
                this->handleButton(in.down, static_cast<int>(in.button) + 1, !in.player2);
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        s.frame++;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto& s = vbot::State::get();
        if (s.mode == vbot::Mode::Recording && PlayLayer::get() && button >= 1 && button <= 3) {
            s.macro.inputs.push_back({s.frame, static_cast<vb::Button>(button - 1), down, !isPlayer1});
            if (s.step.frame == s.frame) keepSnapshot(s, s.step);
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(VbPlayLayer, PlayLayer) {
    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        if (checkpoint) checkpoint->setTag(static_cast<int>(vbot::State::get().frame));
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto& s = vbot::State::get();
        vbot::Renderer::get().onRestart();

        CheckpointObject* cp = nullptr;
        if (m_checkpointArray && m_checkpointArray->count() > 0) {
            cp = static_cast<CheckpointObject*>(m_checkpointArray->lastObject());
        }

        if (!cp) {
            s.frame = 0;
            s.nextInput = 0;
            s.nextSnap = 0;
            if (s.mode == vbot::Mode::Recording) {
                s.macro.inputs.clear();
                s.macro.snaps.clear();
                s.step.frame = 0xFFFFFFFFu;
            }
            return;
        }

        uint32_t frame;
        if (cp->getTag() >= 0) {
            frame = static_cast<uint32_t>(cp->getTag());
        } else {
            frame = static_cast<uint32_t>(std::llround(
                cp->m_gameState.m_levelTime * vbot::Config::get().effectiveTps()));
        }
        s.frame = frame;

        auto& inputs = s.macro.inputs;
        if (s.mode == vbot::Mode::Recording) {
            std::erase_if(inputs, [frame](vb::Input const& in) { return in.frame >= frame; });
            std::erase_if(s.macro.snaps, [frame](vb::Snapshot const& sn) { return sn.frame >= frame; });
            s.forceSnap = true;

            bool held[3][2] = {};
            for (auto const& in : inputs) held[static_cast<int>(in.button)][in.player2] = in.down;
            for (int b = 0; b < 3; b++) {
                for (int p = 0; p < 2; p++) {
                    if (held[b][p]) inputs.push_back({frame, static_cast<vb::Button>(b), false, p == 1});
                }
            }
        }

        auto it = std::lower_bound(inputs.begin(), inputs.end(), frame,
                                   [](vb::Input const& in, uint32_t f) { return in.frame < f; });
        s.nextInput = static_cast<size_t>(it - inputs.begin());

        auto const& snaps = s.macro.snaps;
        auto sit = std::lower_bound(snaps.begin(), snaps.end(), frame,
                                    [](vb::Snapshot const& sn, uint32_t f) { return sn.frame < f; });
        s.nextSnap = static_cast<size_t>(sit - snaps.begin());
    }

    void togglePracticeMode(bool practiceMode) {
        PlayLayer::togglePracticeMode(practiceMode);
        if (!practiceMode && vbot::State::get().mode == vbot::Mode::Recording) {
            vbot::toggleRecord();
        }
    }

    void onQuit() {
        if (vbot::State::get().mode == vbot::Mode::Recording) vbot::toggleRecord();
        PlayLayer::onQuit();
    }
};
