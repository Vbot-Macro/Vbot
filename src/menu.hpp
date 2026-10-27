// menu.hpp - the full Vbot menu: a popup with three tabs (Macro, Cheats, Render).
// Every value is saved as soon as it changes.
#pragma once
#include <algorithm>
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

#include <vector>

#include "actions.hpp"
#include "settings.hpp"
#include "stats.hpp"
#include "util.hpp"

using namespace geode::prelude;

class VbotMenu : public Popup {
protected:
    static constexpr int PAGE_COUNT = 3;
    static inline bool s_open = false;

    std::vector<CCNode*> m_pageNodes[PAGE_COUNT];
    std::vector<TextInput*> m_pageInputs[PAGE_COUNT];
    CCMenuItemToggler* m_tabs[PAGE_COUNT] = {};
    TextInput* m_nameInput = nullptr;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_cheatStatus1 = nullptr; // measured physics steps per second
    CCLabelBMFont* m_cheatStatus2 = nullptr; // hitbox drawing readout
    size_t m_savedCount = 0; // cached so the status line doesn't read the folder every tick

    // --- small builders -----------------------------------------------------------------------

    CCMenuItemSpriteExtra* addButton(int page, char const* text, SEL_MenuHandler cb, CCPoint pos,
                                     float scale = .7f, int tag = 0) {
        auto* spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", .8f);
        spr->setScale(scale);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, cb);
        btn->setTag(tag);
        m_buttonMenu->addChildAtPosition(btn, Anchor::Center, pos);
        m_pageNodes[page].push_back(btn);
        return btn;
    }

    CCLabelBMFont* addLabel(int page, char const* text, CCPoint pos, float scale = .4f) {
        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(scale);
        m_mainLayer->addChildAtPosition(label, Anchor::Center, pos);
        m_pageNodes[page].push_back(label);
        return label;
    }

    CCMenuItemToggler* addToggle(int page, char const* text, bool value, SEL_MenuHandler cb, CCPoint pos) {
        auto* toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, .7f);
        toggle->m_notClickable = true; // we flip it ourselves in the callback
        toggle->toggle(value);
        m_buttonMenu->addChildAtPosition(toggle, Anchor::Center, pos);
        m_pageNodes[page].push_back(toggle);

        auto* label = CCLabelBMFont::create(text, "bigFont.fnt");
        label->setScale(.45f);
        label->setAnchorPoint({0.f, .5f});
        m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(pos.x + 22.f, pos.y));
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

    // --- pages --------------------------------------------------------------------------------

    void buildMacroPage() {
        auto& cfg = vbot::Config::get();

        m_nameInput = addInput(0, "Macro name", cfg.macroName, 190.f, CommonFilter::ID, {0.f, 52.f},
                               [](std::string const& s) {
                                   auto& c = vbot::Config::get();
                                   c.macroName = vbot::util::sanitizeName(s);
                                   c.save();
                               });
        m_nameInput->setMaxCharCount(40);

        for (int dir : {-1, 1}) {
            auto* arrow = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
            arrow->setScale(.7f);
            arrow->setFlipX(dir > 0);
            auto* btn = CCMenuItemSpriteExtra::create(arrow, this, menu_selector(VbotMenu::onCycle));
            btn->setTag(dir);
            m_buttonMenu->addChildAtPosition(btn, Anchor::Center, ccp(dir * 125.f, 52.f));
            m_pageNodes[0].push_back(btn);
        }

        addButton(0, "Record", menu_selector(VbotMenu::onRecord), {-95.f, 0.f});
        addButton(0, "Replay", menu_selector(VbotMenu::onReplay), {0.f, 0.f});
        addButton(0, "Stop", menu_selector(VbotMenu::onStop), {95.f, 0.f});

        m_status = addLabel(0, "", {0.f, -34.f}, .42f);
        addLabel(0, "Macros are saved in config/t1fy.vbot/Macros", {0.f, -100.f}, .3f);
    }

    void buildCheatsPage() {
        auto& cfg = vbot::Config::get();

        // Two columns of switches. Labels are short so they stay inside the popup.
        addToggle(1, "Noclip", cfg.noclip, menu_selector(VbotMenu::onNoclip), {-150.f, 70.f});
        addToggle(1, "Hitboxes", cfg.hitboxes, menu_selector(VbotMenu::onHitboxes), {10.f, 70.f});
        addToggle(1, "TPS Bypass", cfg.tpsBypass, menu_selector(VbotMenu::onTpsToggle), {-150.f, 34.f});
        addToggle(1, "Speed hack", cfg.speedHack, menu_selector(VbotMenu::onSpeedToggle), {-150.f, -2.f});

        addInput(1, "TPS (240+)", std::to_string(cfg.tps), 100.f, CommonFilter::Uint, {-95.f, -44.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.tps = vbot::util::parseIntClamped(s, vbot::NORMAL_TPS, 10000, c.tps);
                     c.save();
                 });
        addInput(1, "Speed (0.1-10)", fmt::format("{:g}", cfg.speed), 100.f, CommonFilter::Float, {95.f, -44.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     auto parsed = geode::utils::numFromString<float>(s);
                     if (parsed.isOk()) c.speed = std::clamp(parsed.unwrap(), 0.1f, 10.f);
                     c.save();
                 });

        addLabel(1, "More TPS = more precise physics, not a faster game. Speed changes how fast it plays.", {0.f, -68.f}, .26f);
        m_cheatStatus1 = addLabel(1, "", {0.f, -86.f}, .3f);
        m_cheatStatus2 = addLabel(1, "", {0.f, -99.f}, .3f);
        addLabel(1, "Noclip, TPS other than 240 and speed other than 1 stop completions from counting.", {0.f, -116.f}, .26f);
    }

    void buildRenderPage() {
        auto& cfg = vbot::Config::get();

        addInput(2, "Width", std::to_string(cfg.renderWidth), 150.f, CommonFilter::Uint, {-100.f, 58.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderWidth = vbot::util::roundDownToEven(vbot::util::parseIntClamped(s, 144, 7680, c.renderWidth));
                     c.save();
                 });
        addInput(2, "Height", std::to_string(cfg.renderHeight), 150.f, CommonFilter::Uint, {100.f, 58.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderHeight = vbot::util::roundDownToEven(vbot::util::parseIntClamped(s, 144, 4320, c.renderHeight));
                     c.save();
                 });
        addInput(2, "Render FPS", std::to_string(cfg.renderFps), 150.f, CommonFilter::Uint, {-100.f, 16.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderFps = vbot::util::parseIntClamped(s, 1, 240, c.renderFps);
                     c.save();
                 });
        addInput(2, "Bitrate (Mbps)", std::to_string(cfg.bitrateMbps), 150.f, CommonFilter::Uint, {100.f, 16.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.bitrateMbps = vbot::util::parseIntClamped(s, 1, 500, c.bitrateMbps);
                     c.save();
                 });
        addInput(2, "Codec", cfg.codec, 150.f, CommonFilter::Any, {-100.f, -26.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.codec = s.empty() ? std::string("libx264") : s;
                     c.save();
                 });
        addInput(2, "Output name", cfg.renderName, 150.f, CommonFilter::ID, {100.f, -26.f},
                 [](std::string const& s) {
                     auto& c = vbot::Config::get();
                     c.renderName = vbot::util::sanitizeName(s, "render");
                     c.save();
                 });

        addToggle(2, "Include level audio", cfg.renderAudio, menu_selector(VbotMenu::onRenderAudio),
                  {-80.f, -48.f});

        addButton(2, "Render", menu_selector(VbotMenu::onRender), {0.f, -72.f}, .8f);
        addLabel(2, "Pick the macro on the Macro tab, press Render, then restart the level.", {0.f, -92.f}, .3f);
        addLabel(2, "Videos are saved in config/t1fy.vbot/Renders", {0.f, -106.f}, .3f);
    }

    // --- setup --------------------------------------------------------------------------------

    bool init() {
        if (!Popup::init(420.f, 280.f)) return false;
        this->setTitle("Vbot");

        // tabs
        char const* names[PAGE_COUNT] = {"Macro", "Cheats", "Render"};
        for (int i = 0; i < PAGE_COUNT; i++) {
            auto* off = ButtonSprite::create(names[i], "goldFont.fnt", "GJ_button_01.png", .8f);
            auto* on = ButtonSprite::create(names[i], "goldFont.fnt", "GJ_button_02.png", .8f);
            off->setScale(.6f);
            on->setScale(.6f);
            auto* tab = CCMenuItemToggler::create(off, on, this, menu_selector(VbotMenu::onTab));
            tab->m_notClickable = true;
            tab->setTag(i);
            m_buttonMenu->addChildAtPosition(tab, Anchor::Center, ccp((i - 1) * 100.f, 100.f));
            m_tabs[i] = tab;
        }

        this->buildMacroPage();
        this->buildCheatsPage();
        this->buildRenderPage();
        this->showPage(0);

        m_savedCount = vbot::listMacros().size();
        this->updateStatus(0.f);
        this->schedule(schedule_selector(VbotMenu::updateStatus), 0.25f);

        s_open = true;
        return true;
    }

    void showPage(int page) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            bool on = i == page;
            for (auto* node : m_pageNodes[i]) node->setVisible(on);
            for (auto* input : m_pageInputs[i]) input->setEnabled(on);
            m_tabs[i]->toggle(on);
        }
    }

    void updateStatus(float) {
        if (!m_status) return;
        auto& s = vbot::State::get();
        std::string text;
        switch (s.mode) {
            case vbot::Mode::Idle:
                text = fmt::format("Idle  |  {} saved macros", m_savedCount);
                break;
            case vbot::Mode::Recording:
                text = fmt::format("Recording...  {} inputs so far", s.macro.inputs.size());
                break;
            case vbot::Mode::Replaying:
                text = fmt::format("Replay armed  |  {} inputs  |  restart the level", s.macro.inputs.size());
                break;
        }
        m_status->setString(text.c_str());

        // Cheats tab readouts: what the game actually did during the last second of play.
        auto& st = vbot::Stats::get();
        if (m_cheatStatus1) {
            std::string phys = (st.stepsPerSec == 0 && st.updatesPerSec == 0)
                ? std::string("Physics: no data yet, play a level first")
                : fmt::format("Physics: {} steps/s, {} updates/s, step 1/{}{}{}", st.stepsPerSec, st.updatesPerSec,
                              st.lastStepDt > 0 ? static_cast<int>(1.0 / st.lastStepDt + 0.5) : 0,
                              vbot::Config::get().tpsUnavailable ? std::string(" [TPS bypass unavailable here]")
                                                                  : (st.tpsMethod[0] ? fmt::format(" [{}]", st.tpsMethod) : std::string()),
                              st.bypassIgnored ? " (bypass ignored by game)" : "");
            m_cheatStatus1->setString(phys.c_str());
        }
        if (m_cheatStatus2) {
            std::string box = st.hitboxVertices < 0
                ? std::string("Hitboxes: not drawn yet")
                : fmt::format("Hitboxes: {} vertices drawn on the last frame{}", st.hitboxVertices,
                              st.hitboxNodeCreated ? " (Vbot made the draw node)" : "");
            m_cheatStatus2->setString(box.c_str());
        }
    }

    // --- callbacks ----------------------------------------------------------------------------

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
        vbot::stopAll();
        this->updateStatus(0.f);
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
