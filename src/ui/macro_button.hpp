// macro_button.hpp - the macro_button.png icon on the pause screen.
//   tap  -> opens the Vbot menu
//   drag -> moves the button (position is saved)
//   size and opacity come from the mod settings
//   the icon turns red while a macro is recording
#pragma once
#include <Geode/Geode.hpp>

#include "menu.hpp"

using namespace geode::prelude;

class MacroButton : public CCNode, public CCTouchDelegate {
protected:
    CCSprite* m_icon = nullptr;
    float m_size = 60.f;
    CCPoint m_touchStart;
    CCPoint m_nodeStart;
    bool m_pressed = false;
    bool m_dragging = false;

    bool init() override {
        if (!CCNode::init()) return false;

        m_size = static_cast<float>(Mod::get()->getSettingValue<double>("button-size"));
        float opacity = static_cast<float>(Mod::get()->getSettingValue<double>("button-opacity"));

        // resources/macro_button.png (500x500). Falls back to a plain button if it's missing.
        m_icon = CCSprite::create(Mod::get()->expandSpriteName("macro_button.png").c_str());
        if (!m_icon) m_icon = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
        m_icon->setScale(m_size / m_icon->getContentSize().width);
        m_icon->setOpacity(static_cast<GLubyte>(255.f * opacity / 100.f));
        this->addChild(m_icon);

        this->schedule(schedule_selector(MacroButton::refreshTint), 0.25f);
        this->refreshTint(0.f);
        return true;
    }

    void refreshTint(float) {
        bool recording = vbot::State::get().mode == vbot::Mode::Recording;
        m_icon->setColor(recording ? ccc3(255, 90, 90) : ccc3(255, 255, 255));
    }

public:
    static MacroButton* create() {
        auto* ret = new MacroButton();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onEnter() override {
        CCNode::onEnter();
        CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
    }

    void onExit() override {
        CCDirector::get()->getTouchDispatcher()->removeDelegate(this);
        CCNode::onExit();
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (!this->isVisible() || VbotMenu::isOpen()) return false;
        auto local = this->convertToNodeSpace(touch->getLocation());
        if (!m_icon->boundingBox().containsPoint(local)) return false;
        m_pressed = true;
        m_dragging = false;
        m_touchStart = touch->getLocation();
        m_nodeStart = this->getPosition();
        return true;
    }

    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (!m_pressed) return;
        auto delta = ccpSub(touch->getLocation(), m_touchStart);
        if (!m_dragging && ccpLength(delta) > 10.f) m_dragging = true;
        if (m_dragging) {
            auto win = CCDirector::get()->getWinSize();
            auto p = ccpAdd(m_nodeStart, delta);
            p.x = std::clamp(p.x, 0.f, win.width);
            p.y = std::clamp(p.y, 0.f, win.height);
            this->setPosition(p);
        }
    }

    void ccTouchEnded(CCTouch*, CCEvent*) override {
        if (!m_pressed) return;
        m_pressed = false;
        if (m_dragging) {
            Mod::get()->setSavedValue<float>("macro-button-x", this->getPositionX());
            Mod::get()->setSavedValue<float>("macro-button-y", this->getPositionY());
        } else {
            if (auto* menu = VbotMenu::create()) menu->show();
        }
        m_dragging = false;
    }

    void ccTouchCancelled(CCTouch*, CCEvent*) override {
        m_pressed = false;
        m_dragging = false;
    }
};
