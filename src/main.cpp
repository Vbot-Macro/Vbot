// main.cpp - Vbot: frame counter, input recorder, replay injection, restart handling.
// (The pause-menu button lives in pause_hooks.cpp / macro_button.hpp.)
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "actions.hpp"
#include "macro.hpp"
#include "renderer.hpp"
#include "settings.hpp"
#include "stats.hpp"

using namespace geode::prelude;

// --- Position snapshots ------------------------------------------------------------------------
// To keep a replay exactly on track, a recording also stores the position of the players (both of
// them in a 2-player level) at the START of some physics steps: every step that has an input, and
// one step every 1/8 s. Replay puts a player back to its snapshot, but only if it has drifted, so an
// exact replay is never touched. Taking the state at the start of the step on both sides means
// recording and replay look at exactly the same moment.

namespace {

constexpr int SNAPS_PER_SECOND = 8;

vb::PlayerState readState(PlayerObject* p) {
    return {p->getPositionX(), p->getPositionY(), static_cast<float>(p->m_yVelocity)};
}

// The state of the player(s) at the start of the current step.
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

// One snapshot per frame. Rewinding cuts everything from the rewind frame on, so new snapshots
// always go at the end.
void keepSnapshot(vbot::State& s, vb::Snapshot const& sn) {
    auto& snaps = s.macro.snaps;
    if (!snaps.empty() && snaps.back().frame >= sn.frame) return;
    snaps.push_back(sn);
}

// Puts the players back to a snapshot if they have drifted from it.
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
        // The wave's vertical speed isn't a normal velocity, so forcing it is what makes a wave break.
        if (!p->m_isDart) p->m_yVelocity = st.yVel;
    }
}

} // namespace

// --- Physics-step hooks: count frames, record inputs, inject replay inputs ---------------------

class $modify(VbBaseGameLayer, GJBaseGameLayer) {
    // Runs once per physics step. We use it as the frame counter and as the moment to inject inputs.
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto& s = vbot::State::get();

        if (!PlayLayer::get()) {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }

        vbot::Stats::get().lastStepDt = dt;
        vbot::Stats::get().procCounter++; // for the steps-per-second readout on the Cheats tab

        if (s.mode == vbot::Mode::Recording) {
            // The state at the start of this step. It is kept if this step has an input (see
            // handleButton), and every 1/8 s regardless, and right after a respawn.
            s.step = captureSnapshot(this, s.frame);
            uint32_t every = static_cast<uint32_t>(
                std::max(1, vbot::Config::get().effectiveTps() / SNAPS_PER_SECOND));
            if (s.forceSnap || s.frame % every == 0) {
                keepSnapshot(s, s.step);
                s.forceSnap = false;
            }
        }

        if (s.mode == vbot::Mode::Replaying) {
            // Snapshots first, so the inputs below act on the recorded state.
            auto const& snaps = s.macro.snaps;
            while (s.nextSnap < snaps.size() && snaps[s.nextSnap].frame <= s.frame) {
                auto const& sn = snaps[s.nextSnap++];
                if (sn.frame == s.frame) applySnapshot(this, sn);
            }

            auto const& inputs = s.macro.inputs;
            while (s.nextInput < inputs.size() && inputs[s.nextInput].frame <= s.frame) {
                auto const& in = inputs[s.nextInput++];
                // Game buttons: 1 = jump, 2 = left, 3 = right. vb::Button is 0-based.
                this->handleButton(in.down, static_cast<int>(in.button) + 1, !in.player2);
            }
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        s.frame++;
    }

    // Every player input goes through here.
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& s = vbot::State::get();
        if (s.mode == vbot::Mode::Recording && PlayLayer::get() && button >= 1 && button <= 3) {
            s.macro.inputs.push_back({s.frame, static_cast<vb::Button>(button - 1), down, !isPlayer1});
            // Keep the state from the start of this step, the moment replay will restore it.
            if (s.step.frame == s.frame) keepSnapshot(s, s.step);
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

// --- Checkpoints and restarts -----------------------------------------------------------------
// Recording works in practice mode only, and it keeps going across deaths until you stop it.
// Every checkpoint remembers the frame it was made on (stored in the checkpoint object itself, so
// deleted or replaced checkpoints can't get out of step). When you respawn from a checkpoint, the
// macro is cut back to that frame and recording continues from there, so a macro is built up over
// as many attempts as you like. Restarting from the very beginning (no checkpoint) starts over.

class $modify(VbPlayLayer, PlayLayer) {
    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        if (checkpoint) checkpoint->setTag(static_cast<int>(vbot::State::get().frame));
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        auto& s = vbot::State::get();
        vbot::Renderer::get().onRestart(); // starts an armed render

        CheckpointObject* cp = nullptr;
        if (m_checkpointArray && m_checkpointArray->count() > 0) {
            cp = static_cast<CheckpointObject*>(m_checkpointArray->lastObject());
        }

        if (!cp) {
            // Fresh start from the beginning.
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

        // Respawn from the last checkpoint: rewind to the frame it was made on.
        // (If the checkpoint has no tag, work the frame out from the level time it saved.)
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
            s.forceSnap = true; // the first step after the respawn records the restored state

            // The game releases every button on respawn, so close any press that is still open.
            bool held[3][2] = {};
            for (auto const& in : inputs) held[static_cast<int>(in.button)][in.player2] = in.down;
            for (int b = 0; b < 3; b++) {
                for (int p = 0; p < 2; p++) {
                    if (held[b][p]) inputs.push_back({frame, static_cast<vb::Button>(b), false, p == 1});
                }
            }
        }

        // Replay: carry on from the first input at or after this frame.
        auto it = std::lower_bound(inputs.begin(), inputs.end(), frame,
                                   [](vb::Input const& in, uint32_t f) { return in.frame < f; });
        s.nextInput = static_cast<size_t>(it - inputs.begin());

        auto const& snaps = s.macro.snaps;
        auto sit = std::lower_bound(snaps.begin(), snaps.end(), frame,
                                    [](vb::Snapshot const& sn, uint32_t f) { return sn.frame < f; });
        s.nextSnap = static_cast<size_t>(sit - snaps.begin());
    }

    // Turning practice mode off while recording saves the macro, since recording needs practice mode.
    void togglePracticeMode(bool practiceMode) {
        PlayLayer::togglePracticeMode(practiceMode);
        if (!practiceMode && vbot::State::get().mode == vbot::Mode::Recording) {
            vbot::toggleRecord(); // saves
        }
    }

    // Leaving the level while recording saves the macro too.
    void onQuit() {
        if (vbot::State::get().mode == vbot::Mode::Recording) vbot::toggleRecord();
        PlayLayer::onQuit();
    }
};
