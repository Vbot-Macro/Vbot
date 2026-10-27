// actions.hpp - what Record / Replay / Render do, plus where files live.
//
// Files go in Geode's config folder:
//   config/t1fy.vbot/Macros/<name>.vb
//   config/t1fy.vbot/Renders/<name>.mp4
#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>

#include "macro.hpp"
#include "render_guard.hpp"
#include "renderer.hpp"
#include "settings.hpp"
#include "util.hpp"

namespace vbot {

// --- folders ----------------------------------------------------------------------------------

inline std::filesystem::path ensureDir(std::filesystem::path dir) {
    auto res = geode::utils::file::createDirectoryAll(dir);
    (void)res; // if this fails, the later read or write reports the real problem
    return dir;
}

inline std::filesystem::path macrosDir() {
    return ensureDir(geode::Mod::get()->getConfigDir() / "Macros");
}

inline std::filesystem::path rendersDir() {
    return ensureDir(geode::Mod::get()->getConfigDir() / "Renders");
}

inline std::filesystem::path macroPath(std::string const& name) {
    return macrosDir() / (util::sanitizeName(name) + ".vb");
}

// Names (without .vb) of every saved macro, sorted.
inline std::vector<std::string> listMacros() {
    std::vector<std::string> names;
    auto res = geode::utils::file::readDirectory(macrosDir());
    if (!res) return names;
    for (auto const& p : res.unwrap()) {
        if (p.extension() == ".vb") {
            names.push_back(geode::utils::string::pathToString(p.stem()));
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// --- feedback ---------------------------------------------------------------------------------

inline void toast(std::string const& text, geode::NotificationIcon icon = geode::NotificationIcon::Info) {
    geode::Notification::create(text, icon)->show();
}

// --- macro actions ----------------------------------------------------------------------------

inline void toggleRecord() {
    auto& s = State::get();
    auto& cfg = Config::get();

    if (s.mode == Mode::Recording) {
        s.mode = Mode::Idle;
        s.macro.fps = static_cast<uint32_t>(cfg.effectiveTps());
        auto bytes = vb::encode(s.macro);
        auto path = macroPath(cfg.macroName);
        auto res = geode::utils::file::writeBinary(path, bytes);
        if (res) {
            toast(fmt::format("Saved {} inputs to {}.vb", s.macro.inputs.size(),
                              util::sanitizeName(cfg.macroName)),
                  geode::NotificationIcon::Success);
        } else {
            toast("Couldn't save the macro", geode::NotificationIcon::Error);
        }
        return;
    }

    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_isPracticeMode) {
        toast("Recording only works in practice mode. Open a level and turn on practice mode first.",
              geode::NotificationIcon::Warning);
        return;
    }
    if (Renderer::get().rendering()) {
        toast("Can't record while rendering.", geode::NotificationIcon::Warning);
        return;
    }

    s.macro.inputs.clear();
    s.macro.snaps.clear();
    s.step.frame = 0xFFFFFFFFu;
    s.forceSnap = false;
    s.mode = Mode::Recording;
    toast("Recording. Place checkpoints and keep going: every attempt is added until you stop.");
}

// Loads the macro named in the settings and switches to replay mode. Returns false (and says why)
// if it can't. With quiet = true it doesn't show the "restart the level" toast.
inline bool armReplay(bool quiet = false) {
    auto& s = State::get();
    auto& cfg = Config::get();

    auto data = geode::utils::file::readBinary(macroPath(cfg.macroName));
    if (!data) {
        toast(fmt::format("No macro named {} found.", util::sanitizeName(cfg.macroName)),
              geode::NotificationIcon::Error);
        return false;
    }
    auto macro = vb::decode(data.unwrap());
    if (!macro) {
        toast("That macro file is corrupted.", geode::NotificationIcon::Error);
        return false;
    }

    s.macro = std::move(*macro);
    s.mode = Mode::Replaying;
    s.nextInput = 0;
    s.nextSnap = 0;

    if (static_cast<int>(s.macro.fps) != cfg.effectiveTps()) {
        toast(fmt::format("Warning: recorded at {} TPS, but you are at {} TPS.", s.macro.fps,
                          cfg.effectiveTps()),
              geode::NotificationIcon::Warning);
    } else if (!quiet) {
        toast(fmt::format("Loaded {} inputs. Restart the level to replay.", s.macro.inputs.size()),
              geode::NotificationIcon::Success);
    }
    return true;
}

inline void stopAll() {
    Renderer::get().cancel("stopped");
    State::get().mode = Mode::Idle;
}

// Render button: loads the macro and arms the renderer. The video starts on the next level restart.
inline void startRender() {
    if (!canRender()) return;
    if (State::get().mode == Mode::Recording) {
        toast("Stop recording first.", geode::NotificationIcon::Warning);
        return;
    }
    if (!PlayLayer::get()) {
        toast("Open a level first, then press Render.", geode::NotificationIcon::Warning);
        return;
    }
    if (Renderer::get().rendering()) {
        toast("Already rendering. Press Stop to end it.", geode::NotificationIcon::Warning);
        return;
    }
    if (!armReplay(true)) return;
    Renderer::get().arm();
    toast("Render ready. Close the menu and restart the level to start.", geode::NotificationIcon::Success);
}

} // namespace vbot
