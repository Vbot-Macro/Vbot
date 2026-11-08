#pragma once

#include "../settings/render_settings.hpp"

#include <Geode/loader/Event.hpp>

namespace ffmpeg::events {

namespace impl {

    constexpr size_t VTABLE_VERSION = 1;

    using CreateRecorder_t = void*(*)();

    using DeleteRecorder_t = void(*)(void*);

    using InitRecorder_t = geode::Result<>(*)(void*, const RenderSettings&);

    using StopRecorder_t = void(*)(void*);

    using WriteFrame_t = geode::Result<>(*)(void*, std::span<uint8_t const>);

    using GetAvailableCodecs_t = std::vector<std::string>(*)();

    using MixVideoAudio_t = geode::Result<>(*)(const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&);

    using MixVideoRaw_t = geode::Result<>(*)(const std::filesystem::path&, std::span<float>, const std::filesystem::path&);

    struct VTable {

        CreateRecorder_t createRecorder = nullptr;

        DeleteRecorder_t deleteRecorder = nullptr;

        InitRecorder_t initRecorder = nullptr;

        StopRecorder_t stopRecorder = nullptr;

        WriteFrame_t writeFrame = nullptr;

        GetAvailableCodecs_t getAvailableCodecs = nullptr;

        MixVideoAudio_t mixVideoAudio = nullptr;

        MixVideoRaw_t mixVideoRaw = nullptr;

    };

    struct FetchVTableEvent : geode::Event<FetchVTableEvent, bool(VTable&, size_t)> {

        using Event::Event;

    };

    inline VTable& getVTable() {

        static VTable vtable;

        static bool initialized = false;

        if (!initialized) {

            initialized = FetchVTableEvent().send(vtable, VTABLE_VERSION);

        }

        return vtable;

    }

}

class Recorder {

public:

    Recorder() {

        auto& vtable = impl::getVTable();

        if (!vtable.createRecorder) {

            m_ptr = nullptr;

        } else {

            m_ptr = vtable.createRecorder();

        }

    }

    ~Recorder() {

        if (m_ptr) {

            auto& vtable = impl::getVTable();

            if (vtable.deleteRecorder) {

                vtable.deleteRecorder(m_ptr);

            }

        }

    }

    bool isValid() const { return m_ptr != nullptr; }

    geode::Result<> init(RenderSettings const& settings) {

        auto& vtable = impl::getVTable();

        if (!vtable.initRecorder) {

            return geode::Err("FFmpeg API is not available.");

        }

        return vtable.initRecorder(m_ptr, settings);

    }

    void stop() {

        auto& vtable = impl::getVTable();

        if (vtable.stopRecorder) {

            vtable.stopRecorder(m_ptr);

        }

    }

    geode::Result<> writeFrame(std::span<uint8_t const> frameData) {

        auto& vtable = impl::getVTable();

        if (!vtable.writeFrame) {

            return geode::Err("FFmpeg API is not available.");

        }

        return vtable.writeFrame(m_ptr, frameData);

    }

    static std::vector<std::string> getAvailableCodecs() {

        auto& vtable = impl::getVTable();

        if (!vtable.getAvailableCodecs) {

            return {};

        }

        return vtable.getAvailableCodecs();

    }

private:

    void* m_ptr = nullptr;

};

class AudioMixer {

public:

    AudioMixer() = delete;

    static geode::Result<> mixVideoAudio(std::filesystem::path const& videoFile, std::filesystem::path const& audioFile, std::filesystem::path const& outputMp4File) {

        auto& vtable = impl::getVTable();

        if (!vtable.mixVideoAudio) {

            return geode::Err("FFmpeg API is not available.");

        }

        return vtable.mixVideoAudio(videoFile, audioFile, outputMp4File);

    }

    static geode::Result<> mixVideoRaw(std::filesystem::path const& videoFile, std::span<float> raw, std::filesystem::path const& outputMp4File) {

        auto& vtable = impl::getVTable();

        if (!vtable.mixVideoRaw) {

            return geode::Err("FFmpeg API is not available.");

        }

        return vtable.mixVideoRaw(videoFile, raw, outputMp4File);

    }

};

}
