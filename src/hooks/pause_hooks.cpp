#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "../ui/macro_button.hpp"

using namespace geode::prelude;

class $modify(VbPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto win = CCDirector::get()->getWinSize();
        auto* btn = MacroButton::create();
        if (!btn) return;

        btn->setPosition({
            Mod::get()->getSavedValue<float>("macro-button-x", win.width - 60.f),
            Mod::get()->getSavedValue<float>("macro-button-y", 60.f),
        });
        btn->setZOrder(100);
        this->addChild(btn);
    }
};
