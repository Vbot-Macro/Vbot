#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <string>

namespace vbot {

constexpr int NORMAL_TPS = 240;
constexpr int MAX_TPS = 6000;

struct Config {
    std::string macroName = "last";

    bool noclip = false;
    bool hitboxes = false;
    bool tpsBypass = false;
    int tps = NORMAL_TPS;
    bool speedHack = false;
    float speed = 1.0f;
    bool tpsUnavailable = false;
    bool autoclicker = false;
    bool acP1 = true;
    bool acP2 = false;
    int acHold = 1;
    int acRelease = 1;
    bool shbod = false;

    int renderWidth = 1920;
    int renderHeight = 1080;
    int renderFps = 60;
    int bitrateMbps = 30;
    std::string codec = "libx264";
    std::string renderName = "render";
    bool renderAudio = true;

    int theme = 0;
    int accentOverride = -1;

    static Config& get() {
        static Config c = load();
        return c;
    }

    int effectiveTps() const {
        return (tpsBypass && !tpsUnavailable) ? tps : NORMAL_TPS;
    }

    float effectiveSpeed() const {
        return speedHack ? speed : 1.0f;
    }

    bool cheatsActive() const {
        return noclip || effectiveTps() != NORMAL_TPS || effectiveSpeed() != 1.0f;
    }

    void save() const {
        auto* mod = geode::Mod::get();

        mod->setSavedValue<std::string>("macro-name", macroName);

        mod->setSavedValue<bool>("noclip", noclip);
        mod->setSavedValue<bool>("hitboxes", hitboxes);
        mod->setSavedValue<bool>("tps-bypass", tpsBypass);
        mod->setSavedValue<int>("tps", tps);
        mod->setSavedValue<bool>("speed-hack", speedHack);
        mod->setSavedValue<float>("speed", speed);
        mod->setSavedValue<bool>("autoclicker", autoclicker);
        mod->setSavedValue<bool>("ac-p1", acP1);
        mod->setSavedValue<bool>("ac-p2", acP2);
        mod->setSavedValue<int>("ac-hold", acHold);
        mod->setSavedValue<int>("ac-release", acRelease);
        mod->setSavedValue<bool>("shbod", shbod);

        mod->setSavedValue<int>("render-width", renderWidth);
        mod->setSavedValue<int>("render-height", renderHeight);
        mod->setSavedValue<int>("render-fps", renderFps);
        mod->setSavedValue<int>("render-bitrate-mbps", bitrateMbps);
        mod->setSavedValue<std::string>("render-codec", codec);
        mod->setSavedValue<std::string>("render-name", renderName);
        mod->setSavedValue<bool>("render-audio", renderAudio);

        mod->setSavedValue<int>("theme", theme);
        mod->setSavedValue<int>("accent-override", accentOverride);
    }

private:
    static Config load() {
        auto* mod = geode::Mod::get();
        Config c;

        c.macroName = mod->getSavedValue<std::string>("macro-name", c.macroName);

        c.noclip = mod->getSavedValue<bool>("noclip", c.noclip);
        c.hitboxes = mod->getSavedValue<bool>("hitboxes", c.hitboxes);
        c.tpsBypass = mod->getSavedValue<bool>("tps-bypass", c.tpsBypass);
        c.tps = std::clamp(
            mod->getSavedValue<int>("tps", c.tps),
            NORMAL_TPS,
            MAX_TPS
        );
        c.speedHack = mod->getSavedValue<bool>("speed-hack", c.speedHack);
        c.speed = mod->getSavedValue<float>("speed", c.speed);
        c.autoclicker = mod->getSavedValue<bool>("autoclicker", c.autoclicker);
        c.acP1 = mod->getSavedValue<bool>("ac-p1", c.acP1);
        c.acP2 = mod->getSavedValue<bool>("ac-p2", c.acP2);
        c.acHold = std::clamp(mod->getSavedValue<int>("ac-hold", c.acHold), 1, 1000);
        c.acRelease = std::clamp(mod->getSavedValue<int>("ac-release", c.acRelease), 1, 1000);
        c.shbod = mod->getSavedValue<bool>("shbod", c.shbod);

        c.renderWidth = mod->getSavedValue<int>("render-width", c.renderWidth);
        c.renderHeight = mod->getSavedValue<int>("render-height", c.renderHeight);
        c.renderFps = mod->getSavedValue<int>("render-fps", c.renderFps);
        c.bitrateMbps = mod->getSavedValue<int>("render-bitrate-mbps", c.bitrateMbps);
        c.codec = mod->getSavedValue<std::string>("render-codec", c.codec);
        c.renderName = mod->getSavedValue<std::string>("render-name", c.renderName);
        c.renderAudio = mod->getSavedValue<bool>("render-audio", c.renderAudio);

        c.theme = mod->getSavedValue<int>("theme", c.theme);
        c.accentOverride = mod->getSavedValue<int>(
            "accent-override",
            c.accentOverride
        );

        return c;
    }
};

}
