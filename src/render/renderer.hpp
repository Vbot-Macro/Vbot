// renderer.hpp - Vbot's internal renderer (video only).
//
// How it works:
//   1. Render button: loads the macro and arms the renderer.
//   2. You restart the level: rendering starts. From then on the game does NOT run in real time.
//      Every video frame the game is advanced by exactly 1/FPS seconds of game time, the screen is
//      drawn into an off-screen texture of the chosen size, and the pixels go to FFmpeg API.
//   3. Rendering stops when the macro has finished (plus a short tail), the level is completed
//      (plus ~3 seconds), the player dies, you press Stop, or you leave the level.
//
// Talks to FFmpeg API through its events header (src/events.hpp), so Vbot doesn't need to
// link against it. If FFmpeg API isn't installed, the Recorder just isn't valid and we say so.
#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <vector>

#include "events.hpp"
#include "../replay/macro.hpp"
#include "render_guard.hpp"
#include "../settings/settings.hpp"
#include "../assist/util.hpp"

namespace vbot {

class Renderer {
public:
    enum class Phase { Idle, Armed, Rendering };

    static Renderer& get() {
        static Renderer r;
        return r;
    }

    Phase phase() const { return m_phase; }
    bool rendering() const { return m_phase == Phase::Rendering; }
    bool armed() const { return m_phase == Phase::Armed; }

    // Render button: the next level restart starts the video.
    void arm() { m_phase = Phase::Armed; }

    // Stop button, or leaving the level.
    void cancel(char const* why = "stopped") {
        if (m_phase == Phase::Rendering) finish(why);
        else m_phase = Phase::Idle;
    }

    // Call right after PlayLayer::resetLevel.
    void onRestart() {
        if (m_phase == Phase::Armed) begin();
        else if (m_phase == Phase::Rendering && m_frames > 1) finish("the level was restarted");
    }

    void noteLevelComplete() {
        if (rendering() && m_stopIn < 0) m_stopIn = m_fps * 3; // let the end animation play
    }
    void notePlayerDied() {
        if (rendering() && m_stopIn < 0) m_stopIn = m_fps;
    }

    // How many game ticks to run for the next video frame (carries the fraction over, so a 240 TPS
    // game gives exactly 1/FPS seconds of game time per frame even when FPS doesn't divide 240).
    int ticksForNextFrame(int ticksPerSecond) {
        m_tickDebt += static_cast<double>(ticksPerSecond) / static_cast<double>(m_fps);
        int ticks = static_cast<int>(m_tickDebt);
        m_tickDebt -= ticks;
        return ticks;
    }

    // Call after the game ran the ticks for one video frame: grab the screen and encode it.
    void captureFrame() {
        if (!rendering()) return;
        auto* scene = cocos2d::CCDirector::get()->getRunningScene();
        if (!scene || !m_texture || !m_recorder) return;

        m_texture->beginWithClear(0.f, 0.f, 0.f, 1.f);
        scene->visit();
        glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
        m_texture->end();

        auto res = m_recorder->writeFrame(std::span<uint8_t const>(m_pixels.data(), m_pixels.size()));
        if (!res) {
            fail(fmt::format("Encoding failed: {}", res.unwrapErr()));
            return;
        }
        m_frames++;

        if (m_stopIn >= 0) {
            if (--m_stopIn <= 0) finish("done");
            return;
        }
        if (m_frames > static_cast<uint64_t>(m_fps) * 60 * 60) { // safety: one hour of video
            finish("reached the one hour limit");
            return;
        }

        // Macro finished: stop a couple of seconds after its last input.
        auto& s = State::get();
        if (s.mode == Mode::Replaying && s.nextInput >= s.macro.inputs.size()) {
            uint32_t last = s.macro.inputs.empty() ? 0 : s.macro.inputs.back().frame;
            uint32_t tail = 2u * static_cast<uint32_t>(Config::get().effectiveTps());
            if (s.frame > last + tail) finish("the macro finished");
        }
    }

private:
    Phase m_phase = Phase::Idle;
    std::unique_ptr<ffmpeg::events::Recorder> m_recorder;
    cocos2d::CCRenderTexture* m_texture = nullptr;
    std::vector<uint8_t> m_pixels;
    std::filesystem::path m_output;
    int m_width = 0, m_height = 0, m_fps = 60;
    uint64_t m_frames = 0;
    double m_tickDebt = 0.0;
    int m_stopIn = -1; // video frames left before we stop, -1 = not stopping yet

    static void say(std::string const& text, geode::NotificationIcon icon = geode::NotificationIcon::Info) {
        geode::Notification::create(text, icon, 3.f)->show();
    }

    void release() {
        if (m_texture) {
            m_texture->release();
            m_texture = nullptr;
        }
        m_recorder.reset();
        m_pixels.clear();
        m_pixels.shrink_to_fit();
        m_stopIn = -1;
        m_frames = 0;
        m_tickDebt = 0.0;
        m_phase = Phase::Idle;
    }

    void fail(std::string const& msg) {
        if (m_recorder) m_recorder->stop();
        release();
        say(fmt::format("Render failed: {}", msg), geode::NotificationIcon::Error);
    }

    std::filesystem::path pickOutputPath() {
        auto dir = geode::Mod::get()->getConfigDir() / "Renders";
        (void)geode::utils::file::createDirectoryAll(dir);
        std::string base = util::sanitizeName(Config::get().renderName, "render");
        auto path = dir / (base + ".mp4");
        // never overwrite an earlier render
        for (int n = 1; asp::fs::exists(path) && n < 10000; n++) {
            path = dir / fmt::format("{}_{}.mp4", base, n);
        }
        // the FFmpeg API mod takes a std::filesystem::path
        return path.toStdPath().unwrapOr(std::filesystem::path(path.string()));
    }

    void begin() {
        auto& cfg = Config::get();
        if (!ffmpegApiLoaded()) {
            m_phase = Phase::Idle;
            say("FFmpeg API isn't loaded, so there's nothing to render with.", geode::NotificationIcon::Error);
            return;
        }

        m_recorder = std::make_unique<ffmpeg::events::Recorder>();
        if (!m_recorder->isValid()) {
            fail("FFmpeg API didn't answer. Restart the game after installing it.");
            return;
        }

        auto codecs = ffmpeg::events::Recorder::getAvailableCodecs();
        if (!codecs.empty() && std::find(codecs.begin(), codecs.end(), cfg.codec) == codecs.end()) {
            fail(fmt::format("codec '{}' isn't available", cfg.codec));
            return;
        }

        m_width = util::roundDownToEven(cfg.renderWidth);
        m_height = util::roundDownToEven(cfg.renderHeight);
        m_fps = std::clamp(cfg.renderFps, 1, 240);
        m_output = pickOutputPath();

        ffmpeg::RenderSettings settings;
        settings.m_codec = cfg.codec;
        settings.m_pixelFormat = ffmpeg::PixelFormat::RGBA;
        settings.m_doVerticalFlip = true; // OpenGL gives us the picture upside down
        settings.m_bitrate = static_cast<int64_t>(cfg.bitrateMbps) * 1000000;
        settings.m_width = static_cast<uint32_t>(m_width);
        settings.m_height = static_cast<uint32_t>(m_height);
        settings.m_fps = static_cast<uint16_t>(m_fps);
        settings.m_outputFile = m_output;

        auto res = m_recorder->init(settings);
        if (!res) {
            fail(res.unwrapErr());
            return;
        }

        m_texture = cocos2d::CCRenderTexture::create(m_width, m_height, cocos2d::kCCTexture2DPixelFormat_RGBA8888);
        if (!m_texture) {
            fail("couldn't create the off-screen texture (try a smaller resolution)");
            return;
        }
        m_texture->retain();
        m_pixels.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 4, 0);

        m_frames = 0;
        m_tickDebt = 0.0;
        m_stopIn = -1;
        m_phase = Phase::Rendering;
        say(fmt::format("Rendering {}x{} at {} FPS. The game runs slower than normal until it's done.",
                        m_width, m_height, m_fps));
    }

    void finish(char const* why) {
        if (m_phase != Phase::Rendering) {
            release();
            return;
        }
        if (m_recorder) m_recorder->stop();
        auto name = m_output.filename().string();
        uint64_t frames = m_frames;
        int fps = m_fps;
        release();
        State::get().mode = Mode::Idle;
        say(fmt::format("Render saved: {} ({:.1f}s, {})", name, static_cast<double>(frames) / fps, why),
            geode::NotificationIcon::Success);
    }
};

} // namespace vbot
