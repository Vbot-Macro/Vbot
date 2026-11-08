#pragma once
#include <Geode/Geode.hpp>

namespace vbot::ui {

struct Theme {
    char const* name;
    cocos2d::ccColor3B bg;
    cocos2d::ccColor3B panel;
    cocos2d::ccColor3B accent;
};

}
