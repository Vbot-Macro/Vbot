// menu.hpp - the Vbot menu: a themed window with a tab list on the left and panels on the right.
//
//   Replay    session (record / replay / stop), speed and timing (TPS bypass, speed hack), physics readout
//   Assists   noclip, frame windows
//   Visuals   hitboxes: options and colours
//   Render    video settings and the Render button
//   Settings  theme and accent colour
//
// A status line at the bottom always shows the mode, the macro size, TPS and speed.
// Every value is saved as soon as it changes.
#pragma once
#include <algorithm>
#include <Geode/Geode.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <vector>

#include "../assist/frame_windows.hpp"
#include "../assist/hitboxes.hpp"
#include "../bot/actions.hpp"
#include "../bot/stats.hpp"
#include "../settings/settings.hpp"
#include "../util/util.hpp"
#include "themes/themes.hpp"

using namespace geode::prelude;

class VbotMenu : public Popup {
protected:
    static constexpr int PAGE_COUNT = 5;
    static inline bool s_open = false;

    // Layout (all positions are from the centre of the window).
    static constexpr float WIN_W = 480.f, WIN_H = 290.f;
    static constexpr float COL_L = -33.f, COL_R = 145.f, COL_W = 170.f;  // the two panel columns
    static constexpr float TOP = 108.f;                                    // top edge of the panels

    char const* PAGE_NAMES[PAGE_COUNT] = {"Replay", "Assists", "Visuals", "Render", "Settings"};

    std::vector<CCNode*> m_pageNodes[PAGE_COUNT];
    std::vector<TextInput*> m_pageInputs[PAGE_COUNT];
    CCScale9Sprite* m_navBg[PAGE_COUNT] = {};
    int m_page = 0;

    // Things the theme colours.
    CCScale9Sprite* m_window = nullptr;
    CCScale9Sprite* m_sidebar = nullptr;
    std::vector<CCScale9Sprite*> m_panels;
    std::vector<CCLayerColor*> m_bars;

    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_savedLabel = nullptr;   // "N saved macros"
    CCLabelBMFont* m_statusBar = nullptr;    // bottom line
    CCLabelBMFont* m_physics1 = nullptr;     // measured physics steps per second
    CCLabelBMFont* m_physics2 = nullptr;     // hitbox drawing readout
    CCLabelBMFont* m_themeName = nullptr;
    CCLayerColor* m_themeSwatch[3] = {};     // background, panel, accent of the chosen theme
    CCLayerColor* m_hbSwatch[4] = {};        // the four hitbox colours
    CCLayerColor* m_accentSwatch = nullptr;
    size_t m_savedCount = 0;

    // --- builders ------------------------------------------------------------------------------

    CCMenuItemSpriteExtra* addButton(int page, char const* text, SEL_MenuHandler cb, CCPoint pos,
                                     float scale = .6f, int tag = 0) {
        auto* spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", .8f);
        spr->setScale(scale);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setTag(tag);
        m_buttonMenu->addChildAtPosition(btn, Anchor::Center, pos);
        m_pageNodes[page].push_back(btn);
        return btn;
    }

    CCLabelBMFont* addLabel(int page, char const* text, CCPoint pos, float scale = .4f, bool left = false) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(scale);
        if (left) label->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(label, Anchor::Center, pos);
        m_pageNodes[page].push_back(label);
        return label;
    }

    CCMenuItemToggler* addToggle(int page, char const* text, bool value, SEL_MenuHandler cb, CCPoint pos,
                                 int tag = 0) {
        auto* toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, .6f);
        toggle->m_notClickable = true; // we flip it ourselves in the callback
        toggle->setTag(tag);
        toggle->toggle(value);
        m_buttonMenu->addChildAtPosition(toggle, Anchor::Center, pos);
        m_pageNodes[page].push_back(toggle);

        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(.4f);
        label->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(pos.x + 18.f, pos.y));
        m_pageNodes[page].push_back(label);
        return toggle;
    }

    TextInput* addInput(int page, char const* label, std::string const& value, float width,
                        CommonFilter filter, CCPoint pos, geode::Function<void(std::string const&)> onChange) {
        auto* input = TextInput::create(width, label);
        input->setLabel(label);
        input->setCommonFilter(filter);
        input->setString(value);
        input->setCallback(std::move(onChange));
        m_mainLayer->addChildAtPosition(input, Anchor::Center, pos);
        m_pageNodes[page].push_back(input);
        m_pageInputs[page].push_back(input);
        return input;
    }

    // A rounded panel with a title and an accent bar. Everything on it is placed from `top`.
    void addPanel(int page, char const* title, float cx, float top, float w, float h) {
        auto* bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({w, h});
        m_mainLayer->addChildAtPosition(bg, Anchor::Center, ccp(cx, top - h / 2.f));
        bg->setZOrder(-1); // behind the buttons
        m_panels.push_back(bg);
        m_pageNodes[page].push_back(bg);

        auto* bar = CCLayerColor::create({255, 255, 255, 255}, 2.f, 11.f);
        bar->ignoreAnchorPointForPosition(false);
        bar->setAnchorPoint({.5f, .5f});
        m_mainLayer->addChildAtPosition(bar, Anchor::Center, ccp(cx - w / 2.f + 9.f, top - 14.f));
        m_bars.push_back(bar);
        m_pageNodes[page].push_back(bar);

        auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
        label->setScale(.38f);
        label->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(cx - w / 2.f + 16.f, top - 14.f));
        m_pageNodes[page].push_back(label);
    }

    CCLayerColor* makeSwatch(ccColor3B c, float size) {
        auto* sw = CCLayerColor::create({c.r, c.g, c.b, 255}, size, size);
        sw->ignoreAnchorPointForPosition(false);
        sw->setAnchorPoint({.5f, .5f});
        return sw;
    }

    // --- hitbox option plumbing ----------------------------------------------------------------

    static cocos2d::ccColor3B& hbColor(int i) {
        auto& st = vbot::hb::Settings::get();
        switch (i) {
            case 0: return st.hazard;
            case 1: return st.orb;
            case 2: return st.block;
            default: return st.passable;
        }
    }

    static bool& hbToggle(int i) {
        auto& st = vbot::hb::Settings::get();
        switch (i) {
            case 0: return st.fill;
            case 1: return st.playerRotation;
            case 2: return st.player;
            default: return st.showActivated;
        }
    }

    // --- pages ---------------------------------------------------------------------------------

    void buildReplayPage() {
        auto& cfg = vbot::Config::get();

        // Session
        addPanel(0, "Session", COL_L, TOP, COL_W, 130.f);
        m_nameInput = addInput(0, "Macro name", cfg.macroName, 96.f, CommonFilter::ID, {COL_L, TOP - 42.f},
                               [](std::string const& s) {
                                   auto& c = vbot::Config::get();
                                   c.macroName = vbot::util::sanitizeName(s);
                                   c.save();
                               });
        m_nameInput->setMaxCharCount(40);
        for (int dir : {-1, 1}) {
            auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            arrow->setScale(.5f);
            arrow->setFlipX(dir > 0);
            auto* btn = CCMenuItemSpriteExtra::create(arrow, this, menu_selector(VbotMenu::onCycle));
            btn->setTag(dir);
            m_buttonMenu->addChildAtPosition(btn, Anchor::Center, ccp(COL_L + dir * 66.f, TOP - 42.f));
            m_pageNodes[0].push_back(btn);
        }
        addButton(0, "Record", menu_selector(VbotMenu::onRecord), {COL_L - 54.f, TOP - 78.f}, .5f);
        addButton(0, "Replay", menu_selector(VbotMenu::onReplay), {COL_L, TOP - 78.f}, .5f);
        addButton(0, "Stop", menu_selector(VbotMenu::onStop), {COL_L + 54.f, TOP - 78.f}, .5f);
        m_savedLabel = addLabel(0, "", {COL_L, TOP - 108.f}, .3f);

        // Speed and timing
        addPanel(0, "Speed and Timing", COL_R, TOP, COL_W, 130.f);
        addToggle(0, "TPS Bypass", cfg.tpsBypass, menu_selector(VbotMenu::onTpsToggle), {COL_R - 72.f, TOP - 34.f});
        addInput(0, "TPS (240-6000)", std::to_string(cfg.tps), 90.f, CommonFilter::Uint, {COL_R + 32.f, TOP - 58.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.tps = vbot::util::parseIntClamped(s, vbot::NORMAL_TPS, vbot::MAX_TPS, c.tps);
                     c.save();
                 });
        addToggle(0, "Speed hack", cfg.speedHack, menu_selector(VbotMenu::onSpeedToggle), {COL_R - 72.f, TOP - 86.f});
        addInput(0, "Speed (0.1-10)", fmt::format("{:g}", cfg.speed), 90.f, CommonFilter::Float, {COL_R + 32.f, TOP - 110.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     auto parsed = geode::utils::numFromString<float>(s);
                     if (parsed.isOk()) c.speed = std::clamp(parsed.unwrap(), 0.1f, 10.f);
                     c.save();
                 });

        // Physics readout, across the whole width
        float cw = COL_R - COL_L + COL_W;
        float cx = (COL_L + COL_R) / 2.f;
        addPanel(0, "Physics", cx, TOP - 138.f, cw, 62.f);
        m_physics1 = addLabel(0, "", {cx, TOP - 168.f}, .28f);
        m_physics2 = addLabel(0, "", {cx, TOP - 182.f}, .28f);
    }

    void buildAssistsPage() {
        auto& cfg = vbot::Config::get();
        addPanel(1, "Assists", COL_L, TOP, COL_W, 130.f);
        addToggle(1, "Noclip", cfg.noclip, menu_selector(VbotMenu::onNoclip), {COL_L - 72.f, TOP - 38.f});
        addButton(1, "Frame windows", menu_selector(VbotMenu::onFrameWindows), {COL_L, TOP - 78.f}, .6f);
        addLabel(1, "Needs practice mode off.", {COL_L, TOP - 102.f}, .28f);

        addPanel(1, "Frame windows", COL_R, TOP, COL_W, 130.f);
        addLabel(1, "Replays the chosen macro with each", {COL_R, TOP - 42.f}, .28f);
        addLabel(1, "click moved a few frames, to find how", {COL_R, TOP - 56.f}, .28f);
        addLabel(1, "many frames of leeway each one has.", {COL_R, TOP - 70.f}, .28f);
        addLabel(1, "The result shows in the top-left of", {COL_R, TOP - 90.f}, .28f);
        addLabel(1, "the level. TPS must match the macro.", {COL_R, TOP - 104.f}, .28f);
    }

    void buildVisualsPage() {
        auto& cfg = vbot::Config::get();
        char const* names[4] = {"Fill Hitboxes", "Player Rotation", "Player", "Show Activated Orbs"};

        addPanel(2, "Hitboxes", COL_L, TOP, COL_W, 170.f);
        addToggle(2, "Hitboxes", cfg.hitboxes, menu_selector(VbotMenu::onHitboxes), {COL_L - 72.f, TOP - 36.f});
        for (int i = 0; i < 4; i++) {
            addToggle(2, names[i], hbToggle(i), menu_selector(VbotMenu::onHbToggle),
                      {COL_L - 72.f, TOP - 62.f - i * 26.f}, i);
        }

        addPanel(2, "Colours", COL_R, TOP, COL_W, 140.f);
        char const* colorNames[4] = {"Hazards", "Orbs", "Blocks", "Passable"};
        for (int i = 0; i < 4; i++) {
            float y = TOP - 42.f - i * 26.f;
            addLabel(2, colorNames[i], {COL_R - 76.f, y}, .38f, true);
            m_hbSwatch[i] = makeSwatch(hbColor(i), 16.f);
            m_mainLayer->addChildAtPosition(m_hbSwatch[i], Anchor::Center, ccp(COL_R + 18.f, y));
            m_pageNodes[2].push_back(m_hbSwatch[i]);
            addButton(2, "Change", menu_selector(VbotMenu::onHbColor), {COL_R + 62.f, y}, .45f, i);
        }
        addLabel(2, "Player: red box and blue box", {COL_R, TOP - 150.f}, .26f);
    }

    void buildRenderPage() {
        auto& cfg = vbot::Config::get();

        addPanel(3, "Video", COL_L, TOP, COL_W, 200.f);
        float lx = COL_L - 40.f, rx = COL_L + 40.f;
        addInput(3, "Width", std::to_string(cfg.renderWidth), 70.f, CommonFilter::Uint, {lx, TOP - 54.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderWidth = vbot::util::roundDownToEven(vbot::util::parseIntClamped(s, 144, 7680, c.renderWidth));
                     c.save();
                 });
        addInput(3, "Height", std::to_string(cfg.renderHeight), 70.f, CommonFilter::Uint, {rx, TOP - 54.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderHeight = vbot::util::roundDownToEven(vbot::util::parseIntClamped(s, 144, 4320, c.renderHeight));
                     c.save();
                 });
        addInput(3, "Render FPS", std::to_string(cfg.renderFps), 70.f, CommonFilter::Uint, {lx, TOP - 100.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderFps = vbot::util::parseIntClamped(s, 1, 240, c.renderFps);
                     c.save();
                 });
        addInput(3, "Bitrate (Mbps)", std::to_string(cfg.bitrateMbps), 70.f, CommonFilter::Uint, {rx, TOP - 100.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.bitrateMbps = vbot::util::parseIntClamped(s, 1, 500, c.bitrateMbps);
                     c.save();
                 });
        addInput(3, "Codec", cfg.codec, 70.f, CommonFilter::Any, {lx, TOP - 146.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.codec = s.empty() ? std::string("libx264") : s;
                     c.save();
                 });
        addInput(3, "Output name", cfg.renderName, 70.f, CommonFilter::ID, {rx, TOP - 146.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderName = vbot::util::sanitizeName(s, "render");
                     c.save();
                 });

        addPanel(3, "Render", COL_R, TOP, COL_W, 130.f);
        addToggle(3, "Include level audio", cfg.renderAudio, menu_selector(VbotMenu::onRenderAudio),
                  {COL_R - 72.f, TOP - 38.f});
        addButton(3, "Render", menu_selector(VbotMenu::onRender), {COL_R, TOP - 74.f}, .75f);
        addLabel(3, "Pick the macro on the Replay tab,", {COL_R, TOP - 98.f}, .26f);
        addLabel(3, "press Render, then restart the level.", {COL_R, TOP - 110.f}, .26f);
    }

    void buildSettingsPage() {
        addPanel(4, "Theme", COL_L, TOP, COL_W, 130.f);
        m_themeName = addLabel(4, vbot::ui::current().name, {COL_L, TOP - 42.f}, .5f);
        for (int dir : {-1, 1}) {
            auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            arrow->setScale(.5f);
            arrow->setFlipX(dir > 0);
            auto* btn = CCMenuItemSpriteExtra::create(arrow, this, menu_selector(VbotMenu::onThemeCycle));
            btn->setTag(dir);
            m_buttonMenu->addChildAtPosition(btn, Anchor::Center, ccp(COL_L + dir * 66.f, TOP - 42.f));
            m_pageNodes[4].push_back(btn);
        }
        for (int i = 0; i < 3; i++) {
            m_themeSwatch[i] = makeSwatch({255, 255, 255}, 18.f);
            m_mainLayer->addChildAtPosition(m_themeSwatch[i], Anchor::Center, ccp(COL_L + (i - 1) * 26.f, TOP - 70.f));
            m_pageNodes[4].push_back(m_themeSwatch[i]);
        }
        addLabel(4, "Accent", {COL_L - 76.f, TOP - 100.f}, .38f, true);
        m_accentSwatch = makeSwatch({255, 255, 255}, 16.f);
        m_mainLayer->addChildAtPosition(m_accentSwatch, Anchor::Center, ccp(COL_L - 14.f, TOP - 100.f));
        m_pageNodes[4].push_back(m_accentSwatch);
        addButton(4, "Change", menu_selector(VbotMenu::onAccent), {COL_L + 26.f, TOP - 100.f}, .45f);
        addButton(4, "Reset", menu_selector(VbotMenu::onAccentReset), {COL_L + 68.f, TOP - 100.f}, .45f);

        addPanel(4, "About", COL_R, TOP, COL_W, 130.f);
        auto version = geode::Mod::get()->getVersion().toVString();
        addLabel(4, fmt::format("Vbot {}", version).c_str(), {COL_R, TOP - 46.f}, .45f);
        addLabel(4, "Macro recorder and replayer", {COL_R, TOP - 72.f}, .3f);
        addLabel(4, "for Geometry Dash", {COL_R, TOP - 86.f}, .3f);
        addLabel(4, "Licensed under GPL-3.0", {COL_R, TOP - 110.f}, .28f);
    }

    // --- setup ---------------------------------------------------------------------------------

    bool init() {
        if (!Popup::init(WIN_W, WIN_H)) return false;
        this->setTitle("Vbot");
        if (m_bgSprite) m_bgSprite->setVisible(false); // replaced by the themed window below

        m_window = CCScale9Sprite::create("square02b_001.png");
        m_window->setContentSize({WIN_W, WIN_H});
        m_mainLayer->addChildAtPosition(m_window, Anchor::Center);
        m_window->setZOrder(-3);

        // tab list
        m_sidebar = CCScale9Sprite::create("square02b_001.png");
        m_sidebar->setContentSize({110.f, 224.f});
        m_mainLayer->addChildAtPosition(m_sidebar, Anchor::Center, ccp(-177.f, -5.f));
        m_sidebar->setZOrder(-2);

        for (int i = 0; i < PAGE_COUNT; i++) {
            auto* node = CCNode::create();
            node->setContentSize({100.f, 26.f});
            m_navBg[i] = CCScale9Sprite::create("square02b_001.png");
            m_navBg[i]->setContentSize({100.f, 26.f});
            m_navBg[i]->setPosition({50.f, 13.f});
            node->addChild(m_navBg[i]);
            auto* label = CCLabelBMFont::create(PAGE_NAMES[i], "bigFont.fnt");
            label->setScale(.45f);
            label->setAnchorPoint({0.f, .5f});
            label->setPosition({12.f, 13.f});
            node->addChild(label);
            auto* tab = CCMenuItemSpriteExtra::create(node, this, menu_selector(VbotMenu::onTab));
            tab->setTag(i);
            m_buttonMenu->addChildAtPosition(tab, Anchor::Center, ccp(-177.f, 92.f - i * 32.f));
        }

        this->buildReplayPage();
        this->buildAssistsPage();
        this->buildVisualsPage();
        this->buildRenderPage();
        this->buildSettingsPage();

        m_statusBar = CCLabelBMFont::create("", "bigFont.fnt");
        m_statusBar->setScale(.3f);
        m_statusBar->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(m_statusBar, Anchor::Center, ccp(-228.f, -131.f));

        this->applyTheme();
        this->showPage(0);

        m_savedCount = vbot::listMacros().size();
        this->updateStatus(0.f);
        this->schedule(schedule_selector(VbotMenu::updateStatus), 0.25f);

        s_open = true;
        return true;
    }

    void applyTheme() {
        auto th = vbot::ui::current();
        m_window->setColor(th.bg);
        m_window->setOpacity(240);
        m_sidebar->setColor(th.panel);
        m_sidebar->setOpacity(150);
        for (auto* p : m_panels) {
            p->setColor(th.panel);
            p->setOpacity(150);
        }
        for (auto* b : m_bars) b->setColor(th.accent);
        for (int i = 0; i < PAGE_COUNT; i++) {
            m_navBg[i]->setColor(th.accent);
            m_navBg[i]->setOpacity(i == m_page ? 120 : 0);
        }
        if (m_themeName) m_themeName->setString(th.name);
        auto base = vbot::ui::THEMES[vbot::ui::clampedIndex()];
        if (m_themeSwatch[0]) {
            m_themeSwatch[0]->setColor(base.bg);
            m_themeSwatch[1]->setColor(base.panel);
            m_themeSwatch[2]->setColor(th.accent);
        }
        if (m_accentSwatch) m_accentSwatch->setColor(th.accent);
    }

    void showPage(int page) {
        m_page = page;
        for (int i = 0; i < PAGE_COUNT; i++) {
            bool on = i == page;
            for (auto* node : m_pageNodes[i]) node->setVisible(on);
            for (auto* input : m_pageInputs[i]) input->setEnabled(on);
            m_navBg[i]->setOpacity(on ? 120 : 0);
        }
    }

    void updateStatus(float) {
        auto& s = vbot::State::get();
        auto& cfg = vbot::Config::get();

        char const* mode = s.mode == vbot::Mode::Recording ? "Recording"
                         : s.mode == vbot::Mode::Replaying ? "Replay armed" : "Idle";
        if (m_statusBar) {
            auto text = fmt::format("{}   {} inputs   {} TPS   {:g}x speed", mode, s.macro.inputs.size(),
                                    cfg.effectiveTps(), cfg.effectiveSpeed());
            m_statusBar->setString(text.c_str());
        }
        if (m_savedLabel) {
            auto text = fmt::format("{} saved macros", m_savedCount);
            m_savedLabel->setString(text.c_str());
        }

        // Physics readout: what the game actually did during the last second of play.
        auto& st = vbot::Stats::get();
        if (m_physics1) {
            std::string phys = (st.stepsPerSec == 0 && st.updatesPerSec == 0)
                ? std::string("Physics: no data yet, play a level first")
                : fmt::format("Physics: {} steps/s, {} updates/s, step 1/{}{}{}", st.stepsPerSec, st.updatesPerSec,
                              st.lastStepDt > 0 ? static_cast<int>(1.0 / st.lastStepDt + 0.5) : 0,
                              cfg.tpsUnavailable ? std::string(" [TPS bypass unavailable here]")
                                                 : (st.tpsMethod[0] ? fmt::format(" [{}]", st.tpsMethod) : std::string()),
                              st.bypassIgnored ? " (bypass ignored by game)" : "");
            m_physics1->setString(phys.c_str());
        }
        if (m_physics2) {
            std::string box = st.hitboxVertices < 0
                ? std::string("Hitboxes: not drawn yet")
                : fmt::format("Hitboxes: {} vertices drawn on the last frame{}", st.hitboxVertices,
                              st.hitboxNodeCreated ? " (Vbot made the draw node)" : "");
            m_physics2->setString(box.c_str());
        }
    }

    // --- callbacks -----------------------------------------------------------------------------

    void onTab(CCObject* sender) {
        this->showPage(static_cast<CCNode*>(sender)->getTag());
    }

    void onRecord(CCObject*) {
        vbot::toggleRecord();
        m_savedCount = vbot::listMacros().size();
        this->updateStatus(0.f);
    }

    void onReplay(CCObject*) {
        vbot::armReplay();
        this->updateStatus(0.f);
    }

    void onStop(CCObject*) {
        vbot::fw::cancel("stopped");
        vbot::stopAll();
        this->updateStatus(0.f);
    }

    void onFrameWindows(CCObject*) {
        vbot::fw::start();
        if (vbot::fw::active()) this->onClose(nullptr); // close the menu so the level can run
    }

    void onRenderAudio(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        auto& cfg = vbot::Config::get();
        cfg.renderAudio = value;
        cfg.save();
    }

    void onRender(CCObject*) {
        vbot::startRender();
    }

    void onCycle(CCObject* sender) {
        int dir = static_cast<CCNode*>(sender)->getTag();
        auto names = vbot::listMacros();
        if (names.empty()) {
            vbot::toast("No saved macros yet.");
            return;
        }
        auto& cfg = vbot::Config::get();
        int count = static_cast<int>(names.size());
        auto it = std::find(names.begin(), names.end(), vbot::util::sanitizeName(cfg.macroName));
        int idx;
        if (it == names.end()) idx = dir > 0 ? 0 : count - 1;
        else idx = (static_cast<int>(it - names.begin()) + dir + count) % count;
        cfg.macroName = names[idx];
        cfg.save();
        m_nameInput->setString(names[idx]);
    }

    void onNoclip(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        auto& cfg = vbot::Config::get();
        cfg.noclip = value;
        cfg.save();
    }

    void onHitboxes(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        auto& cfg = vbot::Config::get();
        cfg.hitboxes = value;
        cfg.save();

        // Apply right away if a level is open behind the menu.
        if (auto* pl = PlayLayer::get()) {
            pl->updateDebugDrawSettings();
            if (!pl->m_isDebugDrawEnabled && pl->m_debugDrawNode) pl->m_debugDrawNode->clear();
        }
    }

    void onHbToggle(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        hbToggle(toggle->getTag()) = value;
        vbot::hb::Settings::get().save();
    }

    void onHbColor(CCObject* sender) {
        int i = static_cast<CCNode*>(sender)->getTag();
        auto* picker = geode::ColorPickPopup::create(hbColor(i));
        if (!picker) return;
        Ref<VbotMenu> self = this;
        picker->setCallback([self, i](ccColor4B const& c) {
            hbColor(i) = {c.r, c.g, c.b};
            if (self->m_hbSwatch[i]) self->m_hbSwatch[i]->setColor({c.r, c.g, c.b});
            vbot::hb::Settings::get().save();
        });
        picker->show();
    }

    void onSpeedToggle(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        auto& cfg = vbot::Config::get();
        cfg.speedHack = value;
        cfg.save();
    }

    void onTpsToggle(CCObject* sender) {
        auto* toggle = static_cast<CCMenuItemToggler*>(sender);
        bool value = !toggle->isToggled();
        toggle->toggle(value);
        auto& cfg = vbot::Config::get();
        cfg.tpsBypass = value;
        cfg.save();
    }

    void onThemeCycle(CCObject* sender) {
        int dir = static_cast<CCNode*>(sender)->getTag();
        auto& cfg = vbot::Config::get();
        int n = vbot::ui::themeCount();
        cfg.theme = (vbot::ui::clampedIndex() + dir + n) % n;
        cfg.save();
        this->applyTheme();
    }

    void onAccent(CCObject*) {
        auto* picker = geode::ColorPickPopup::create(vbot::ui::current().accent);
        if (!picker) return;
        Ref<VbotMenu> self = this;
        picker->setCallback([self](ccColor4B const& c) {
            auto& cfg = vbot::Config::get();
            cfg.accentOverride = vbot::ui::packColor({c.r, c.g, c.b});
            cfg.save();
            self->applyTheme();
        });
        picker->show();
    }

    void onAccentReset(CCObject*) {
        auto& cfg = vbot::Config::get();
        cfg.accentOverride = -1;
        cfg.save();
        this->applyTheme();
    }

public:
    static bool isOpen() { return s_open; }

    ~VbotMenu() {
        s_open = false;
    }

    static VbotMenu* create() {
        auto* ret = new VbotMenu();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};
