// menu.cpp - the Vbot menu, drawn with ImGui (gd-imgui-cocos).
//
//   Replay    session (record / replay / stop), physics readout
//   Assists   TPS bypass, autoclicker (soon), noclip, speed hack
//   Visuals   hitboxes: options and colours, layout mode and show hitboxes on death (soon)
//   Render    video settings and the Render button
//   Settings  theme and accent colour
//
// A status line at the bottom always shows the mode, the macro size, TPS and speed.
// Every value is saved as soon as it changes. Colours come from the chosen theme (see themes/).
#include "menu.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <string>

#include "../assist/actions.hpp"
#include "../assist/util.hpp"
#include "../hooks/stats.hpp"
#include "../physics/hitboxes.hpp"
#include "../settings/settings.hpp"
#include "themes/themes.hpp"

using namespace geode::prelude;

namespace {

bool s_open = false;
int s_page = 0;
bool s_styled = false;   // sizes and font scale are set once per ImGui context
float s_scale = 1.f;

constexpr int PAGE_COUNT = 5;
constexpr char const* PAGE_NAMES[PAGE_COUNT] = {"Replay", "Assists", "Visuals", "Render", "Settings"};

// --- colours ---------------------------------------------------------------------------------

ImVec4 toVec(ccColor3B c, float a = 1.f) {
    return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, a);
}

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.f);
}

// Sizes only change when the window size does, colours change with the theme, so they are separate.
void applySizes(float displayHeight) {
    s_scale = std::clamp(displayHeight / 800.f, 1.f, 2.5f);
    auto& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 10.f;
    st.ChildRounding = 8.f;
    st.FrameRounding = 6.f;
    st.GrabRounding = 6.f;
    st.PopupRounding = 8.f;
    st.ScrollbarRounding = 6.f;
    st.WindowBorderSize = 1.f;
    st.ChildBorderSize = 1.f;
    st.FrameBorderSize = 0.f;
    st.WindowPadding = ImVec2(12.f, 12.f);
    st.FramePadding = ImVec2(8.f, 5.f);
    st.ItemSpacing = ImVec2(8.f, 8.f);
    st.ScrollbarSize = 12.f;
    st.GrabMinSize = 14.f;
    st.ScaleAllSizes(s_scale);
    st.FontScaleMain = s_scale;
    s_styled = true;
}

void applyColors(vbot::ui::Theme const& th) {
    auto* c = ImGui::GetStyle().Colors;
    ImVec4 bg = toVec(th.bg);
    ImVec4 panel = toVec(th.panel);
    ImVec4 acc = toVec(th.accent);
    ImVec4 white(1.f, 1.f, 1.f, 1.f);
    ImVec4 border = mix(panel, acc, .25f);

    c[ImGuiCol_Text] = ImVec4(.93f, .95f, .98f, 1.f);
    c[ImGuiCol_TextDisabled] = ImVec4(.58f, .62f, .70f, 1.f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_PopupBg] = mix(bg, panel, .6f);
    c[ImGuiCol_Border] = border;
    c[ImGuiCol_FrameBg] = mix(panel, white, .07f);
    c[ImGuiCol_FrameBgHovered] = mix(panel, acc, .25f);
    c[ImGuiCol_FrameBgActive] = mix(panel, acc, .40f);
    c[ImGuiCol_Button] = mix(panel, acc, .35f);
    c[ImGuiCol_ButtonHovered] = mix(panel, acc, .60f);
    c[ImGuiCol_ButtonActive] = acc;
    ImVec4 sel = acc;
    sel.w = .55f;
    c[ImGuiCol_Header] = sel;
    c[ImGuiCol_HeaderHovered] = ImVec4(acc.x, acc.y, acc.z, .35f);
    c[ImGuiCol_HeaderActive] = ImVec4(acc.x, acc.y, acc.z, .75f);
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = mix(acc, white, .3f);
    c[ImGuiCol_TitleBg] = bg;
    c[ImGuiCol_TitleBgActive] = mix(bg, panel, .6f);
    c[ImGuiCol_TitleBgCollapsed] = bg;
    c[ImGuiCol_Separator] = border;
    c[ImGuiCol_ScrollbarBg] = bg;
    c[ImGuiCol_ScrollbarGrab] = mix(panel, acc, .4f);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(panel, acc, .6f);
    c[ImGuiCol_ScrollbarGrabActive] = acc;
}

// --- small widgets ---------------------------------------------------------------------------

// A panel with an accent bar and a title. `body` draws the contents.
template <class F>
void panel(char const* title, float width, F&& body, float height = 0.f) {
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (height <= 0.f) flags |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(title, ImVec2(width, height), flags);

    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 3.f * s_scale, p.y + h), accent, 2.f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.f * s_scale);
    ImGui::TextUnformatted(title);
    ImGui::Spacing();

    body();
    ImGui::EndChild();
}

float halfWidth() {
    return (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
}

// Whole-number field that applies its value when the edit ends (so typing "5" on the way to
// "500" is not clamped to the minimum), or right away when the +/- buttons are used.
bool intField(char const* id, int& value, int lo, int hi, int step, int stepFast) {
    int temp = value;
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputInt(id, &temp, step, stepFast);
    bool done = ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());
    if (!done) return false;
    value = std::clamp(temp, lo, hi);
    return true;
}

bool floatField(char const* id, float& value, float lo, float hi, float step, float stepFast) {
    float temp = value;
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool changed = ImGui::InputFloat(id, &temp, step, stepFast, "%g");
    bool done = ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());
    if (!done) return false;
    value = std::clamp(temp, lo, hi);
    return true;
}

// Text field backed by a buffer. The buffer follows the saved value unless something is being edited.
bool textField(char const* id, std::string const& saved, char* buf, size_t size, std::string& out) {
    if (!ImGui::IsAnyItemActive()) {
        std::strncpy(buf, saved.c_str(), size - 1);
        buf[size - 1] = 0;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText(id, buf, size);
    if (!ImGui::IsItemDeactivatedAfterEdit()) return false;
    out = buf;
    return true;
}

bool colorField(char const* label, ccColor3B& color) {
    float v[3] = {color.r / 255.f, color.g / 255.f, color.b / 255.f};
    ImGui::PushID(label);
    bool changed = ImGui::ColorEdit3("##c", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    if (changed) {
        color = {static_cast<GLubyte>(v[0] * 255.f + .5f), static_cast<GLubyte>(v[1] * 255.f + .5f),
                 static_cast<GLubyte>(v[2] * 255.f + .5f)};
    }
    return changed;
}

bool checkbox(char const* label, bool& value) {
    return ImGui::Checkbox(label, &value);
}

// A feature that isn't built yet: shown greyed out.
void soon(char const* label) {
    bool off = false;
    ImGui::BeginDisabled();
    ImGui::Checkbox((std::string(label) + " (soon)").c_str(), &off);
    ImGui::EndDisabled();
}

// --- hitbox settings -------------------------------------------------------------------------

ccColor3B& hbColor(int i) {
    auto& st = vbot::hb::Settings::get();
    switch (i) {
        case 0: return st.hazard;
        case 1: return st.orb;
        case 2: return st.block;
        default: return st.passable;
    }
}

// --- pages -----------------------------------------------------------------------------------

void pageReplay(float width) {
    auto& cfg = vbot::Config::get();

    static char nameBuf[64] = "";
    static size_t savedCount = vbot::listMacros().size();

    panel("Session", width, [&] {
        std::string out;
        float arrow = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(arrow * 2.f + ImGui::GetStyle().ItemSpacing.x * 2.f));
        if (!ImGui::IsAnyItemActive()) {
            std::strncpy(nameBuf, cfg.macroName.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = 0;
        }
        ImGui::InputText("##name", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            cfg.macroName = vbot::util::sanitizeName(nameBuf);
            cfg.save();
        }
        for (int dir : {-1, 1}) {
            ImGui::SameLine();
            if (ImGui::Button(dir < 0 ? "<##prev" : ">##next", ImVec2(arrow, arrow))) {
                auto names = vbot::listMacros();
                if (names.empty()) {
                    vbot::toast("No saved macros yet.");
                } else {
                    int count = static_cast<int>(names.size());
                    auto it = std::find(names.begin(), names.end(), vbot::util::sanitizeName(cfg.macroName));
                    int idx = it == names.end() ? (dir > 0 ? 0 : count - 1)
                                                : (static_cast<int>(it - names.begin()) + dir + count) % count;
                    cfg.macroName = names[idx];
                    cfg.save();
                    std::strncpy(nameBuf, names[idx].c_str(), sizeof(nameBuf) - 1);
                }
            }
        }

        float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.f) / 3.f;
        if (ImGui::Button("Record", ImVec2(bw, 0))) {
            vbot::toggleRecord();
            savedCount = vbot::listMacros().size();
        }
        ImGui::SameLine();
        if (ImGui::Button("Replay", ImVec2(bw, 0))) vbot::armReplay();
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(bw, 0))) vbot::stopAll();
        ImGui::TextDisabled("%zu saved macros", savedCount);
    });

    panel("Physics", width, [&] {
        auto& st = vbot::Stats::get();
        if (st.stepsPerSec == 0 && st.updatesPerSec == 0) {
            ImGui::TextDisabled("Physics: no data yet, play a level first");
        } else {
            std::string line = fmt::format("Physics: {} steps/s, {} updates/s, step 1/{}", st.stepsPerSec,
                                           st.updatesPerSec,
                                           st.lastStepDt > 0 ? static_cast<int>(1.0 / st.lastStepDt + 0.5) : 0);
            if (cfg.tpsUnavailable) line += " [TPS bypass unavailable here]";
            else if (st.tpsMethod[0]) line += fmt::format(" [{}]", st.tpsMethod);
            if (st.bypassIgnored) line += " (bypass ignored by game)";
            ImGui::TextWrapped("%s", line.c_str());
        }
        if (st.hitboxVertices < 0) {
            ImGui::TextDisabled("Hitboxes: not drawn yet");
        } else {
            ImGui::TextWrapped("Hitboxes: %d vertices drawn on the last frame%s", static_cast<int>(st.hitboxVertices),
                               st.hitboxNodeCreated ? " (Vbot made the draw node)" : "");
        }
    });
}

void pageAssists(float width) {
    auto& cfg = vbot::Config::get();

    panel("Assists", width, [&] {
        if (checkbox("TPS Bypass", cfg.tpsBypass)) cfg.save();
        if (intField("##tps", cfg.tps, vbot::NORMAL_TPS, vbot::MAX_TPS, 60, 240)) cfg.save();
        ImGui::TextDisabled("TPS (240-6000)");
        ImGui::Spacing();
        soon("Autoclicker");
        ImGui::Spacing();
        if (checkbox("Noclip", cfg.noclip)) cfg.save();
        ImGui::Spacing();
        if (checkbox("Speedhack", cfg.speedHack)) cfg.save();
        if (floatField("##speed", cfg.speed, 0.1f, 10.f, 0.1f, 0.5f)) cfg.save();
        ImGui::TextDisabled("Speed (0.1-10)");
    });
}

void pageVisuals(float) {
    auto& cfg = vbot::Config::get();
    auto& hb = vbot::hb::Settings::get();
    float half = halfWidth();

    panel("Hitboxes", half, [&] {
        if (checkbox("Hitboxes", cfg.hitboxes)) {
            cfg.save();
            // Apply right away if a level is open behind the menu.
            if (auto* pl = PlayLayer::get()) {
                pl->updateDebugDrawSettings();
                if (!pl->m_isDebugDrawEnabled && pl->m_debugDrawNode) pl->m_debugDrawNode->clear();
            }
        }
        ImGui::Separator();
        bool changed = false;
        changed |= checkbox("Fill Hitboxes", hb.fill);
        changed |= checkbox("Player Rotation", hb.playerRotation);
        changed |= checkbox("Player", hb.player);
        changed |= checkbox("Show Activated Orbs", hb.showActivated);
        if (changed) hb.save();
        ImGui::Separator();
        soon("Layout Mode");
        soon("SHBOD (Show Hitboxes On Death)");
    });
    ImGui::SameLine();
    panel("Colours", half, [&] {
        char const* names[4] = {"Hazards", "Orbs", "Blocks", "Passable"};
        bool changed = false;
        for (int i = 0; i < 4; i++) changed |= colorField(names[i], hbColor(i));
        if (changed) hb.save();
        ImGui::TextDisabled("Player: red box and blue box");
    });
}

void pageRender(float) {
    auto& cfg = vbot::Config::get();
    float half = halfWidth();

    static char codecBuf[32] = "";
    static char outBuf[64] = "";

    panel("Video", half, [&] {
        std::string out;
        ImGui::TextDisabled("Width");
        if (intField("##w", cfg.renderWidth, 144, 7680, 2, 100)) {
            cfg.renderWidth = vbot::util::roundDownToEven(cfg.renderWidth);
            cfg.save();
        }
        ImGui::TextDisabled("Height");
        if (intField("##h", cfg.renderHeight, 144, 4320, 2, 100)) {
            cfg.renderHeight = vbot::util::roundDownToEven(cfg.renderHeight);
            cfg.save();
        }
        ImGui::TextDisabled("Render FPS");
        if (intField("##fps", cfg.renderFps, 1, 240, 1, 10)) cfg.save();
        ImGui::TextDisabled("Bitrate (Mbps)");
        if (intField("##br", cfg.bitrateMbps, 1, 500, 1, 10)) cfg.save();
        ImGui::TextDisabled("Codec");
        if (textField("##codec", cfg.codec, codecBuf, sizeof(codecBuf), out)) {
            cfg.codec = out.empty() ? std::string("libx264") : out;
            cfg.save();
        }
        ImGui::TextDisabled("Output name");
        if (textField("##out", cfg.renderName, outBuf, sizeof(outBuf), out)) {
            cfg.renderName = vbot::util::sanitizeName(out, "render");
            cfg.save();
        }
    });
    ImGui::SameLine();
    panel("Render", half, [&] {
        if (checkbox("Include level audio", cfg.renderAudio)) cfg.save();
        ImGui::Spacing();
        if (ImGui::Button("Render", ImVec2(-FLT_MIN, 0))) vbot::startRender();
        ImGui::TextWrapped("Pick the macro on the Replay tab, press Render, then restart the level.");
    });
}

void pageSettings(float) {
    auto& cfg = vbot::Config::get();
    float half = halfWidth();

    panel("Theme", half, [&] {
        int current = vbot::ui::clampedIndex();
        for (int i = 0; i < vbot::ui::themeCount(); i++) {
            auto const& t = vbot::ui::THEMES[i];
            ImGui::PushID(i);
            ImGui::ColorButton("##sw", toVec(t.accent), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                               ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
            ImGui::SameLine();
            if (ImGui::Selectable(t.name, i == current, 0, ImVec2(0, ImGui::GetFrameHeight()))) {
                cfg.theme = i;
                cfg.save();
            }
            ImGui::PopID();
        }
        ImGui::Separator();
        auto th = vbot::ui::current();
        ccColor3B accent = th.accent;
        if (colorField("Accent", accent)) {
            cfg.accentOverride = vbot::ui::packColor(accent);
            cfg.save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            cfg.accentOverride = -1;
            cfg.save();
        }
    });
    ImGui::SameLine();
    panel("About", half, [&] {
        ImGui::Text("Vbot %s", geode::Mod::get()->getVersion().toVString().c_str());
        ImGui::TextDisabled("Macro recorder and replayer for Geometry Dash");
        ImGui::Spacing();
        ImGui::TextDisabled("Licensed under GPL-3.0");
    });
}

// --- window ----------------------------------------------------------------------------------

void statusLine() {
    auto& state = vbot::State::get();
    auto& cfg = vbot::Config::get();
    char const* mode = state.mode == vbot::Mode::Recording ? "Recording"
                     : state.mode == vbot::Mode::Replaying ? "Replay armed" : "Idle";
    ImGui::TextDisabled("%s   %zu inputs   %d TPS   %gx speed", mode, state.macro.inputs.size(),
                        static_cast<int>(cfg.effectiveTps()), static_cast<double>(cfg.effectiveSpeed()));
}

void draw() {
    if (!s_open) return;

    auto& io = ImGui::GetIO();
    if (!s_styled) applySizes(io.DisplaySize.y);
    applyColors(vbot::ui::current());

    ImVec2 size(std::min(io.DisplaySize.x * .94f, 760.f * s_scale), std::min(io.DisplaySize.y * .92f, 440.f * s_scale));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Appearing,
                            ImVec2(.5f, .5f));

    bool open = true;
    if (ImGui::Begin("Vbot", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        auto& st = ImGui::GetStyle();
        float footer = ImGui::GetTextLineHeightWithSpacing() + st.ItemSpacing.y * 2.f;
        float bodyH = ImGui::GetContentRegionAvail().y - footer;
        float navW = std::max(110.f * s_scale, ImGui::GetContentRegionAvail().x * .2f);

        ImGui::BeginChild("##nav", ImVec2(navW, bodyH), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        for (int i = 0; i < PAGE_COUNT; i++) {
            if (ImGui::Selectable(PAGE_NAMES[i], s_page == i, 0, ImVec2(0, ImGui::GetFrameHeight() * 1.15f))) s_page = i;
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##content", ImVec2(0, bodyH), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        float w = ImGui::GetContentRegionAvail().x;
        switch (s_page) {
            case 0: pageReplay(w); break;
            case 1: pageAssists(w); break;
            case 2: pageVisuals(w); break;
            case 3: pageRender(w); break;
            default: pageSettings(w); break;
        }
        ImGui::EndChild();

        ImGui::Separator();
        statusLine();
    }
    ImGui::End();
    if (!open) s_open = false;
}

} // namespace

bool VbotMenu::isOpen() { return s_open; }
void VbotMenu::open() { s_open = true; }
void VbotMenu::close() { s_open = false; }
void VbotMenu::toggle() { s_open = !s_open; }

$on_mod(Loaded) {
    ImGuiCocos::get()
        .setup([] {
            ImGui::GetIO().IniFilename = nullptr; // no imgui.ini
            s_styled = false;                       // sizes are set on the next draw
        })
        .draw([] { draw(); });
}
