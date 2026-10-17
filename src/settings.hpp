// settings.hpp - every Vbot setting that the menu changes. Saved between sessions.
#pragma once
#include <Geode/Geode.hpp>
#include <string>

namespace vbot {

constexpr int NORMAL_TPS = 240;

struct Config {
    // macros
    std::string macroName = "last";
    bool accurate = true; // record player position with every input, snap back to it on replay

    // cheats
    bool noclip = false;
    bool hitboxes = false;
    bool tpsBypass = false;
    int tps = NORMAL_TPS;
    bool speedHack = false;
    float speed = 1.0f;       // game speed multiplier while the speed hack is on
    // Not saved: set when the TPS bypass can't run on this device/game version. It then counts as off.
    bool tpsUnavailable = false;

    // renderer
    int renderWidth = 1920;
    int renderHeight = 1080;
    int renderFps = 60;
    int bitrateMbps = 30;
    std::string codec = "libx264";
    std::string renderName = "render";
    bool renderAudio = true; // add the level's song to the video

    static Config& get() {
        static Config c = load();
        return c;
    }

    // The tick rate the game is actually running at.
    int effectiveTps() const {
        return (tpsBypass && !tpsUnavailable) ? tps : NORMAL_TPS;
    }

    // Game speed multiplier actually in effect.
    float effectiveSpeed() const {
        return speedHack ? speed : 1.0f;
    }

    // True when something is on that makes a completion not count.
    bool cheatsActive() const {
        return noclip || effectiveTps() != NORMAL_TPS || effectiveSpeed() != 1.0f;
    }

    void save() const {
        auto* mod = geode::Mod::get();
        mod->setSavedValue<std::string>("macro-name", macroName);
        mod->setSavedValue<bool>("accurate", accurate);
        mod->setSavedValue<bool>("noclip", noclip);
        mod->setSavedValue<bool>("hitboxes", hitboxes);
        mod->setSavedValue<bool>("tps-bypass", tpsBypass);
        mod->setSavedValue<int>("tps", tps);
        mod->setSavedValue<bool>("speed-hack", speedHack);
        mod->setSavedValue<float>("speed", speed);
        mod->setSavedValue<int>("render-width", renderWidth);
        mod->setSavedValue<int>("render-height", renderHeight);
        mod->setSavedValue<int>("render-fps", renderFps);
        mod->setSavedValue<int>("render-bitrate-mbps", bitrateMbps);
        mod->setSavedValue<std::string>("render-codec", codec);
        mod->setSavedValue<std::string>("render-name", renderName);
        mod->setSavedValue<bool>("render-audio", renderAudio);
    }

private:
    static Config load() {
        auto* mod = geode::Mod::get();
        Config c;
        c.macroName = mod->getSavedValue<std::string>("macro-name", c.macroName);
        c.accurate = mod->getSavedValue<bool>("accurate", c.accurate);
        c.noclip = mod->getSavedValue<bool>("noclip", c.noclip);
        c.hitboxes = mod->getSavedValue<bool>("hitboxes", c.hitboxes);
        c.tpsBypass = mod->getSavedValue<bool>("tps-bypass", c.tpsBypass);
        c.tps = mod->getSavedValue<int>("tps", c.tps);
        c.speedHack = mod->getSavedValue<bool>("speed-hack", c.speedHack);
        c.speed = mod->getSavedValue<float>("speed", c.speed);
        c.renderWidth = mod->getSavedValue<int>("render-width", c.renderWidth);
        c.renderHeight = mod->getSavedValue<int>("render-height", c.renderHeight);
        c.renderFps = mod->getSavedValue<int>("render-fps", c.renderFps);
        c.bitrateMbps = mod->getSavedValue<int>("render-bitrate-mbps", c.bitrateMbps);
        c.codec = mod->getSavedValue<std::string>("render-codec", c.codec);
        c.renderName = mod->getSavedValue<std::string>("render-name", c.renderName);
        c.renderAudio = mod->getSavedValue<bool>("render-audio", c.renderAudio);
        return c;
    }
};

} // namespace vbot
