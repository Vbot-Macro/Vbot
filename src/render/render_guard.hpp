#pragma once
#include <Geode/Geode.hpp>

namespace vbot {

constexpr char const* FFMPEG_API_ID = "eclipse.ffmpeg-api";

inline bool ffmpegApiLoaded() {
    return geode::Loader::get()->isModLoaded(FFMPEG_API_ID);
}

inline bool canRender() {
    if (ffmpegApiLoaded()) return true;

    geode::createQuickPopup(
        "FFmpeg API missing",
        "Rendering needs the <cy>FFmpeg API</c> mod, and it isn't installed (or is disabled).\n"
        "Install it from the in-game mod list, then restart the game.",
        "Close", "Open page",
        [](auto*, bool openPage) {
            if (openPage) {
                geode::openInfoPopup(FFMPEG_API_ID);
            }
        }
    );
    return false;
}

}
