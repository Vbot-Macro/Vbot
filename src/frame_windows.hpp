// frame_windows.hpp - the whole frame window feature in one file.
//
// A click's "window" is how many frames wide the range is in which that click still works: press
// it any earlier or later than that range and the attempt dies. A window of 1 means exactly one
// frame works, i.e. frame perfect. (2 frames is NOT frame perfect.)
//
// The file has three parts:
//   1. vbot::windows  the rows of the counter, and the Planner: the logic that decides which
//                     attempts to run and works out each click's window from the answers
//   2. FrameWindowCounter  the counter drawn in the top-left of a level
//   3. vbot::fw       the measurement that runs inside the game (the "Frame windows" button)
#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "actions.hpp"
#include "macro.hpp"
#include "settings.hpp"
#include "vb.hpp"

// ======================================================================================
// 1. rows and logic
// ======================================================================================

namespace vbot::windows {

// The rows, in the order they are drawn (top to bottom), like the counter in NaN's videos.
struct Bucket {
    const char* label;
    uint32_t lo, hi;     // inclusive range of window sizes
    uint32_t color;      // 0xRRGGBB for the label
};

inline constexpr std::array<Bucket, 7> BUCKETS = {{
    {"9-12", 9, 12, 0x6C8CFF},
    {"7-8",  7, 8,  0x58C4FF},
    {"5-6",  5, 6,  0x5CE08A},
    {"4",    4, 4,  0xD8F26A},
    {"3",    3, 3,  0xFFE14D},
    {"2",    2, 2,  0xFF9A3C},
    {"1",    1, 1,  0xFF4D4D},   // frame perfect
}};

// Windows of 13 frames or more aren't counted (the counter stops at 9-12). 0 = no window at all.
inline int bucketOf(uint32_t window) {
    for (size_t i = 0; i < BUCKETS.size(); i++) {
        if (window >= BUCKETS[i].lo && window <= BUCKETS[i].hi) return static_cast<int>(i);
    }
    return -1;
}

// counts[i] = number of clicks whose window falls in BUCKETS[i].
inline std::array<uint32_t, 7> tally(std::vector<uint32_t> const& windows) {
    std::array<uint32_t, 7> counts{};
    for (auto w : windows) {
        int b = bucketOf(w);
        if (b >= 0) counts[b]++;
    }
    return counts;
}

inline std::string rowText(size_t i) {
    return std::string(BUCKETS[i].label) + ":";
}

// --- finding the windows -------------------------------------------------------------------------
//
// A window is found by testing: replay the macro with one click moved 1, 2, 3... frames earlier
// until the attempt dies, then 1, 2, 3... frames later until it dies. The window is the number of
// shifts that survived, in both directions, plus the original (so a click that dies when moved
// either way has a window of 1: frame perfect).
//
// "Survived" means the attempt reached `endFrame`: a little past the NEXT click, so a click that
// only fails later (the player is on course to hit something) is still caught. This is an
// approximation; a click that causes a death long after the next click will look too wide.
//
// The Planner only decides what to test and collects the answers. The game does the testing.

struct Trial {
    std::vector<vb::Input> inputs; // the macro to replay for this attempt
    uint32_t endFrame = 0;         // surviving until this frame counts as a pass
};

class Planner {
public:
    static constexpr int MAX_SHIFT = 12;      // look at most this many frames each way
    static constexpr uint32_t AFTER = 20;     // frames past the next click the attempt must survive
    static constexpr uint32_t TAIL = 120;     // frames past the last input, for the last click

    explicit Planner(vb::Macro macro) : m_inputs(std::move(macro.inputs)) {
        std::stable_sort(m_inputs.begin(), m_inputs.end(),
                         [](vb::Input const& a, vb::Input const& b) { return a.frame < b.frame; });
        for (size_t i = 0; i < m_inputs.size(); i++) {
            if (m_inputs[i].down) m_clicks.push_back(i);
        }
        m_last = m_inputs.empty() ? 0 : m_inputs.back().frame;
    }

    size_t clickCount() const { return m_clicks.size(); }
    size_t clickIndex() const { return m_k; }
    bool baselineFailed() const { return m_baselineFailed; }
    std::vector<uint32_t> const& windows() const { return m_windows; }

    // The next attempt to run. Returns false when everything has been measured.
    bool next(Trial& out) {
        for (;;) {
            if (m_phase == Phase::Done) return false;
            if (m_phase == Phase::Baseline) {
                out.inputs = m_inputs;
                out.endFrame = m_last + TAIL;
                m_pending = true;
                return true;
            }
            if (m_k >= m_clicks.size()) { m_phase = Phase::Done; return false; }

            uint32_t press = m_inputs[m_clicks[m_k]].frame;
            if (m_phase == Phase::Earlier) {
                if (m_s > MAX_SHIFT || press < static_cast<uint32_t>(m_s)) {
                    m_phase = Phase::Later; m_s = 1; m_l = 0;
                    continue;
                }
                out = build(-m_s);
            } else {
                if (m_s > MAX_SHIFT || 1 + m_e + m_l >= 13) { finishClick(); continue; }
                out = build(m_s);
            }
            m_pending = true;
            return true;
        }
    }

    // The result of the attempt `next` just returned.
    void report(bool survived) {
        if (!m_pending) return;
        m_pending = false;
        switch (m_phase) {
            case Phase::Baseline:
                if (survived) { m_phase = Phase::Earlier; m_k = 0; m_s = 1; m_e = 0; }
                else { m_baselineFailed = true; m_phase = Phase::Done; }
                break;
            case Phase::Earlier:
                if (survived) { m_e = m_s; m_s++; }
                else { m_phase = Phase::Later; m_s = 1; m_l = 0; }
                break;
            case Phase::Later:
                if (survived) { m_l = m_s; m_s++; }
                else finishClick();
                break;
            case Phase::Done: break;
        }
    }

private:
    enum class Phase { Baseline, Earlier, Later, Done };

    void finishClick() {
        m_windows.push_back(static_cast<uint32_t>(1 + m_e + m_l));
        m_k++;
        m_phase = Phase::Earlier;
        m_s = 1; m_e = 0; m_l = 0;
    }

    // The macro with click m_k (and the release that goes with it) moved by `delta` frames.
    Trial build(int delta) const {
        Trial t;
        t.inputs = m_inputs;
        size_t pi = m_clicks[m_k];
        auto const& press = m_inputs[pi];

        auto move = [&](size_t i) {
            int64_t f = static_cast<int64_t>(t.inputs[i].frame) + delta;
            t.inputs[i].frame = static_cast<uint32_t>(std::max<int64_t>(0, f));
        };
        move(pi);
        for (size_t i = pi + 1; i < m_inputs.size(); i++) { // the matching release
            auto const& in = m_inputs[i];
            if (!in.down && in.button == press.button && in.player2 == press.player2) { move(i); break; }
        }
        std::stable_sort(t.inputs.begin(), t.inputs.end(),
                         [](vb::Input const& a, vb::Input const& b) { return a.frame < b.frame; });

        t.endFrame = m_last + TAIL;
        for (size_t i = pi + 1; i < m_inputs.size(); i++) {
            if (m_inputs[i].down) { t.endFrame = m_inputs[i].frame + AFTER; break; }
        }
        return t;
    }

    std::vector<vb::Input> m_inputs;
    std::vector<size_t> m_clicks;   // indexes of the presses
    std::vector<uint32_t> m_windows;
    uint32_t m_last = 0;
    Phase m_phase = Phase::Baseline;
    size_t m_k = 0;
    int m_s = 1, m_e = 0, m_l = 0;
    bool m_pending = false;
    bool m_baselineFailed = false;
};

} // namespace vbot::windows

// ======================================================================================
// 2. the counter on screen
// ======================================================================================

// Draws the frame window counter in the top-left of a level:
//   9-12: 0
//   7-8:  0
//   5-6:  0
//   4:    1
//   3:    1
//   2:    0
//   1:    0
// Each row is "label:" in its own colour, and the count in white.
namespace vbot {

class FrameWindowCounter : public cocos2d::CCNode {
public:
    static FrameWindowCounter* create(std::array<uint32_t, 7> const& counts) {
        auto* n = new FrameWindowCounter();
        if (n && n->init(counts)) { n->autorelease(); return n; }
        delete n;
        return nullptr;
    }

    void setCounts(std::array<uint32_t, 7> const& counts) {
        for (size_t i = 0; i < windows::BUCKETS.size(); i++) m_values[i]->setString(std::to_string(counts[i]).c_str());
    }

protected:
    cocos2d::CCLabelBMFont* m_values[7] = {};

    bool init(std::array<uint32_t, 7> const& counts) {
        if (!CCNode::init()) return false;
        constexpr float ROW = 14.f, SCALE = 0.38f, VALUE_X = 44.f;
        for (size_t i = 0; i < windows::BUCKETS.size(); i++) {
            float y = (windows::BUCKETS.size() - 1 - i) * ROW; // first row on top
            auto const& b = windows::BUCKETS[i];

            auto* label = cocos2d::CCLabelBMFont::create(windows::rowText(i).c_str(), "bigFont.fnt");
            label->setScale(SCALE);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({0.f, y});
            label->setColor({static_cast<GLubyte>(b.color >> 16), static_cast<GLubyte>(b.color >> 8), static_cast<GLubyte>(b.color)});
            this->addChild(label);

            m_values[i] = cocos2d::CCLabelBMFont::create(std::to_string(counts[i]).c_str(), "bigFont.fnt");
            m_values[i]->setScale(SCALE);
            m_values[i]->setAnchorPoint({0.f, 0.5f});
            m_values[i]->setPosition({VALUE_X, y});
            this->addChild(m_values[i]);
        }
        this->setContentSize({VALUE_X + 30.f, windows::BUCKETS.size() * ROW});
        return true;
    }
};

} // namespace vbot

// ======================================================================================
// 3. the measurement inside the game
// ======================================================================================

// The "Frame windows" measurement: replays the selected macro over and over, each
// time with one click moved a few frames, and counts how wide each click's window is
// (see frame_windows.hpp for how the answers are worked out).
//
// How it runs inside the game:
//  - start() loads the macro and restarts the level from the beginning.
//  - Every restart (PlayLayer::resetLevel) is the start of the next attempt: onReset() reports how the
//    last one went and puts the next macro in (silently: no toasts, except when it can't start). After a death the game restarts by itself, after a pass
//    we restart it (tick()).
//  - Position snapshots are NOT used, because they would put the player back on track and hide the
//    failure. Noclip is ignored while the test runs.
//  - The game runs faster while it is going (boost()).
// Leaving the level, Stop or a second start cancels it.
namespace vbot::fw {

struct Session {
    bool active = false;
    bool started = false;     // the first restart has happened
    bool succeeded = false;   // this attempt reached its end frame (or the level)
    bool failed = false;      // this attempt died
    bool resetCalled = false; // we already asked for the next restart
    uint32_t ticks = 0;       // frames since the last restart, to notice a stuck test
    uint32_t steps = 0;       // physics steps since the last restart
    std::unique_ptr<windows::Planner> planner;
    windows::Trial trial;
    vb::Macro saved;          // what was in State::macro before the test

    static Session& get() {
        static Session s;
        return s;
    }
};

// The last finished result (counts per bucket), shown in the corner of every level until the game closes.
inline bool g_hasResult = false;
inline std::array<uint32_t, 7> g_counts{};
constexpr int OVERLAY_TAG = 0x7646;

inline bool active() { return Session::get().active; }

// How much faster the game runs while the test is going.
inline float boost() { return Session::get().active ? 6.f : 1.f; }

inline void showOverlay() {
    auto* ui = UILayer::get();
    if (!ui) return;
    if (auto* old = ui->getChildByTag(OVERLAY_TAG)) old->removeFromParent();
    if (!g_hasResult) return;
    auto* node = FrameWindowCounter::create(g_counts);
    if (!node) return;
    float top = cocos2d::CCDirector::get()->getWinSize().height - 20.f;
    node->setPosition({8.f, top - 6 * 14.f});
    node->setTag(OVERLAY_TAG);
    ui->addChild(node, 50);
}

inline void restoreMacro() {
    auto& s = State::get();
    auto& se = Session::get();
    s.macro = std::move(se.saved);
    se.saved = vb::Macro{};
    s.mode = Mode::Idle;
    s.frame = 0;
    s.nextInput = 0;
    s.nextSnap = 0;
}

// Stops the test because the user asked (Stop, leaving the level). No toast.
inline void cancel(char const* why) {
    auto& se = Session::get();
    if (!se.active) return;
    (void)why;
    se.active = false;
    se.planner.reset();
    restoreMacro();
}

// The test stopped by itself (it couldn't start, or something went wrong). This is the only toast it shows.
inline void autoDisable(char const* why) {
    geode::log::warn("Vbot frame windows: disabled itself: {}", why);
    auto& se = Session::get();
    if (se.active) {
        se.active = false;
        se.planner.reset();
        restoreMacro();
    }
    toast("The Frame Window option has been automatically disabled.", geode::NotificationIcon::Warning);
}

inline void finish() {
    auto& se = Session::get();
    bool baselineFailed = se.planner->baselineFailed();
    auto windowsFound = se.planner->windows();
    se.active = false;
    se.planner.reset();
    restoreMacro();

    if (baselineFailed) {
        geode::log::warn("Vbot frame windows: the macro dies when replayed without corrections");
        toast("The Frame Window option has been automatically disabled.", geode::NotificationIcon::Warning);
        return;
    }
    g_counts = windows::tally(windowsFound);
    g_hasResult = true;
    showOverlay();
}

// Called from the menu button.
inline void start() {
    auto& se = Session::get();
    auto& s = State::get();
    auto& cfg = Config::get();

    auto* pl = PlayLayer::get();
    if (!pl) { autoDisable("no level is open"); return; }
    if (pl->m_isPracticeMode) {
        autoDisable("practice mode is on");
        return;
    }
    if (s.mode == Mode::Recording) { autoDisable("recording is on"); return; }
    if (Renderer::get().rendering()) { autoDisable("rendering is on"); return; }
    if (se.active) cancel("restarted");

    // Remember what was loaded, then load the chosen macro.
    vb::Macro before = std::move(s.macro);
    Mode beforeMode = s.mode;
    if (!armReplay(true)) { s.macro = std::move(before); s.mode = beforeMode; return; }
    if (static_cast<int>(s.macro.fps) != cfg.effectiveTps()) {
        autoDisable("the macro's TPS doesn't match the current TPS");
        s.macro = std::move(before);
        s.mode = beforeMode;
        return;
    }
    if (s.macro.inputs.empty()) {
        autoDisable("the macro has no inputs");
        s.macro = std::move(before);
        s.mode = beforeMode;
        return;
    }

    vb::Macro loaded = std::move(s.macro);
    se.saved = std::move(before);
    loaded.snaps.clear();
    se.planner = std::make_unique<windows::Planner>(std::move(loaded));
    se.active = true;
    se.started = false;
    se.succeeded = se.failed = se.resetCalled = false;
    se.ticks = 0;
    // The level is paused behind the menu. Restart it with the pause screen's own "restart from the
    // start" button, which is the exact code path the game uses (resume, close the pause screen,
    // restart and start moving). Doing those steps by hand left the player stuck on the first frame.
    geode::log::info("Vbot frame windows: starting, {} clicks", se.planner->clickCount());
    PauseLayer* pause = nullptr;
    if (auto* scene = cocos2d::CCDirector::get()->getRunningScene()) pause = scene->getChildByType<PauseLayer>(0);
    if (pause) pause->onRestartFull(nullptr);
    else pl->resumeAndRestart(true);
}

// Every restart of the level. Reports the last attempt and loads the next one.
inline void onReset() {
    auto& se = Session::get();
    if (!se.active) return;
    auto& s = State::get();

    // A restart with no physics step since the last one isn't a real attempt (the game can restart
    // twice in a row): load the same attempt again instead of counting it as a failure.
    bool duplicate = se.started && se.steps == 0 && !se.failed && !se.succeeded;
    if (se.started && !duplicate) se.planner->report(se.succeeded && !se.failed);
    se.started = true;
    se.steps = 0;
    se.succeeded = se.failed = se.resetCalled = false;
    se.ticks = 0;

    if (duplicate) {
        geode::log::debug("Vbot frame windows: ignored a second restart in a row");
    } else {
        windows::Trial t;
        if (!se.planner->next(t)) { finish(); return; }
        se.trial = std::move(t);
    }
    geode::log::debug("Vbot frame windows: attempt for click {} of {}, ends at frame {}", se.planner->clickIndex() + 1, se.planner->clickCount(), se.trial.endFrame);

    s.macro.fps = static_cast<uint32_t>(Config::get().effectiveTps());
    s.macro.inputs = se.trial.inputs;
    s.macro.snaps.clear();
    s.mode = Mode::Replaying;
    s.frame = 0;
    s.nextInput = 0;
    s.nextSnap = 0;
}

// Before each physics step.
inline void onStep(uint32_t frame) {
    auto& se = Session::get();
    if (!se.active || !se.started) return;
    se.steps++;
    if (se.succeeded || se.failed) return;
    if (frame >= se.trial.endFrame) se.succeeded = true;
}

inline void onDeath() {
    auto& se = Session::get();
    if (se.active && se.started) se.failed = true;
}

// Finishing the level counts as a pass. Returns true if the game's own completion should be skipped.
inline bool onComplete() {
    auto& se = Session::get();
    if (!se.active) return false;
    if (se.started && !se.failed) se.succeeded = true;
    return true;
}

// Once per frame, after the game's own update: a passed attempt is over, so start the next one.
inline void tick(PlayLayer* pl) {
    auto& se = Session::get();
    // Stuck: an attempt that never ends or never restarts (a bug, or the level was left in a odd state).
    if (se.active && se.started && ++se.ticks > 6000) { autoDisable("an attempt got stuck"); return; }
    if (!se.active || !se.started || !se.succeeded || se.failed || se.resetCalled) return;
    se.resetCalled = true;
    pl->resetLevelFromStart();
}

} // namespace vbot::fw
