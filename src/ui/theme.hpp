// theme.hpp - what a theme is. Each theme lives in its own file in this folder.
//
// A theme is three colours: the window background, the panels inside it, and the accent (the
// highlighted tab, the bars next to section titles).
#pragma once
#include <Geode/Geode.hpp>

namespace vbot::ui {

struct Theme {
    char const* name;
    cocos2d::ccColor3B bg;
    cocos2d::ccColor3B panel;
    cocos2d::ccColor3B accent;
};

} // namespace vbot::ui
