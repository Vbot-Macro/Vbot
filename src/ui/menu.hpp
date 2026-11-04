// menu.hpp - the Vbot menu. The window itself is drawn with ImGui in menu.cpp.
//
// VbotMenu::open() / close() / toggle() are all the rest of the mod needs.
#pragma once

class VbotMenu {
public:
    static bool isOpen();
    static void open();
    static void close();
    static void toggle();
};
