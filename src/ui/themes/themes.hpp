// themes.hpp - the list of themes and the helpers the menu uses.
//
// To add a theme: copy one of the files in this folder, change its colours, include it here and
// add it to THEMES. The chosen theme and accent override are saved in Config (theme, accentOverride).
#pragma once
#include <Geode/Geode.hpp>

#include <array>

#include "../../settings/settings.hpp"
#include "ember.hpp"
#include "forest.hpp"
#include "halloween.hpp"
#include "midnight.hpp"
#include "ocean.hpp"
#include "theme.hpp"
#include "violet.hpp"

namespace vbot::ui {

inline constexpr std::array<Theme, 6> THEMES = {
    themes::midnight, themes::ocean, themes::violet,
    themes::ember, themes::halloween, themes::forest,
};

inline int themeCount() { return static_cast<int>(THEMES.size()); }

inline int clampedIndex() {
    int i = Config::get().theme;
    return (i < 0 || i >= themeCount()) ? 0 : i;
}

inline Theme current() {
    Theme t = THEMES[clampedIndex()];
    int o = Config::get().accentOverride;
    if (o >= 0 && o <= 0xFFFFFF) {
        t.accent = {static_cast<GLubyte>(o >> 16), static_cast<GLubyte>(o >> 8), static_cast<GLubyte>(o)};
    }
    return t;
}

inline int packColor(cocos2d::ccColor3B c) { return (c.r << 16) | (c.g << 8) | c.b; }

} // namespace vbot::ui
