// step_patch.hpp - lets the TPS bypass choose how many physics steps the game runs per frame.
//
// The game turns the frame time into a step count (about time * 240) inside GJBaseGameLayer::update.
// This file replaces that one calculation so the count comes from g_count. Android and Windows 2.2081
// use a raw byte patch (a hook would change the registers the game keeps for the following steps).
// Windows 2.2091 uses three SafetyHook mid-function hooks. If the spot isn't recognised or another
// mod already changed it, nothing is touched and the TPS bypass stays off.
#pragma once
#include <Geode/Geode.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION >= 22091
#include <safetyhook.hpp>
#include <safetyhook/easy.hpp>
#endif

namespace vbot::stepPatch {

// The step count the patched game code reads each frame. Written by our update() hook.
inline uint32_t g_count = 1;

// Which platforms have a known patch spot for the game version being built for.
#if defined(GEODE_IS_ANDROID64)
#define VBOT_STEP_ANDROID 1
#endif
#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION < 22091
#define VBOT_STEP_WINDOWS 1 // one raw patch (2.2081)
#endif
#if defined(GEODE_IS_WINDOWS) && GEODE_COMP_GD_VERSION >= 22091
#define VBOT_STEP_MIDHOOK 1 // three mid-function hooks (2.2091)
#endif

#if defined(VBOT_STEP_ANDROID) || defined(VBOT_STEP_WINDOWS) || defined(VBOT_STEP_MIDHOOK)
inline constexpr bool PLATFORM = true;
#else
inline constexpr bool PLATFORM = false;
#endif

enum class State { Untried, Ready, Unavailable };
inline State g_state = State::Untried;
inline geode::Patch* g_patch = nullptr;
inline bool g_on = false;
inline std::string g_why; // set when the patch can't be used
inline bool g_whyIsFullMessage = false; // g_why is already the complete toast text

#ifdef VBOT_STEP_ANDROID
// The ten arm64 instructions expected at the spot. Each game version has its own bytes. The patch only
// goes in if they match, and the registers it overwrites are not read again afterwards.
#if GEODE_COMP_GD_VERSION >= 22091
inline constexpr uintptr_t OFFSET = 0x9ed6e4;
inline constexpr size_t BLE_INDEX = 6; // the b.le in the ten
inline constexpr std::array<uint32_t, 10> ORIGINAL = {
    0x1e62100d, 0x1e2e1000, 0x1e6d09cd, 0x1e6241ac, 0x1e26418a,
    0x1e202150, 0x540073cd, 0x1e380140, 0x1e620000, 0xb900d3a0,
};
inline constexpr uint32_t STORE_COUNT = 0xb900d3a0; // str w0, [x29, #0xd0]
#else
inline constexpr uintptr_t OFFSET = 0x89d9f8;
inline constexpr size_t BLE_INDEX = 6;
inline constexpr std::array<uint32_t, 10> ORIGINAL = {
    0x1e62100a, 0x1e6a096a, 0x1e624140, 0x1e264009, 0x1e2e1000,
    0x1e202130, 0x5400758d, 0x1e380120, 0x1e620000, 0xb900e3a0,
};
inline constexpr uint32_t STORE_COUNT = 0xb900e3a0; // str w0, [x29, #0xe0]
#endif

// Replacement: w0 = max(g_count, 1), d0 = w0 as a double, the count stored in the same stack slot,
// then jump past the original ten. The last 8 bytes (appended later) are the address of g_count.
inline constexpr std::array<uint32_t, 8> REPLACEMENT = {
    0x58000100, 0xb9400000, 0x7100041f, 0x1a9fa400,
    0x1e620000, STORE_COUNT, 0x14000004, 0xd503201f,
};
#endif

#ifdef VBOT_STEP_MIDHOOK
// Windows 2.2091: three mid-function hooks inside GJBaseGameLayer::update (offsets into GeometryDash.exe).
//   PHYS_DT     "* 4" becomes "* tps / 60" and that instruction is skipped
//   STEP_COUNT  the loop counter is set so the game runs g_count steps
//   RESTORE_DT  the time given to the steps is (1 / tps) * g_count
// getModifiedDelta returns one step's time while this runs (see cheats.cpp).
inline constexpr uintptr_t HOOK_PHYS_DT = 0x340900;
inline constexpr uintptr_t HOOK_STEP_COUNT = 0x340B18;
inline constexpr uintptr_t HOOK_RESTORE_DT = 0x341BC7;

inline double g_tps = 240.0; // this frame's TPS, set by the update hook
inline std::vector<SafetyHookMid> g_hooks;

// The hooks only act while g_on is set.
inline void midPhysDt(safetyhook::Context& ctx) {
    if (!g_on) return;
    ctx.xmm1.f64[0] *= g_tps / 60.0;
    ctx.rip += 0x08;
}

inline void midStepCount(safetyhook::Context& ctx) {
    if (!g_on) return;
    ctx.r13 = static_cast<uint64_t>(static_cast<int64_t>(2) - static_cast<int64_t>(g_count));
}

inline void midRestoreDt(safetyhook::Context& ctx) {
    if (!g_on) return;
    ctx.xmm9.f64[0] = (1.0 / g_tps) * static_cast<double>(g_count);
}
#endif

#ifdef VBOT_STEP_WINDOWS
// Windows 2.2081: the count math in GJBaseGameLayer::update (offset into GeometryDash.exe).
// Replacement (x86-64): movabs r11, <address of g_count> ; mov r11d, [r11] ; jmp +0x31.
// The original bytes aren't checked, so this build only loads on 2.2081.
inline constexpr uintptr_t OFFSET = 0x237A5B;
inline constexpr std::array<uint8_t, 18> WIN_REPLACEMENT = {
    0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
    0x45, 0x8B, 0x1B,
    0xE9, 0x31, 0x00, 0x00, 0x00,
};
#endif

#ifdef VBOT_STEP_ANDROID
// Find the count math: at OFFSET, or else exactly one match within 64 KB (the b.le's distance may differ).
inline uintptr_t findCountMath() {
    uintptr_t base = geode::base::get();
    auto* at = reinterpret_cast<const uint32_t*>(base + OFFSET);
    if (std::memcmp(at, ORIGINAL.data(), sizeof(ORIGINAL)) == 0) return base + OFFSET;

    auto matches = [](const uint32_t* p) {
        for (size_t i = 0; i < ORIGINAL.size(); i++) {
            if (i == BLE_INDEX) {
                if ((p[i] & 0xFF00001Fu) != 0x5400000Du) return false; // b.le
            } else if (p[i] != ORIGINAL[i]) {
                return false;
            }
        }
        return true;
    };
    constexpr intptr_t RANGE = 0x10000 / 4; // 64 KB either side
    uintptr_t found = 0;
    int count = 0;
    for (intptr_t i = -RANGE; i <= RANGE; i++) {
        if (matches(at + i)) {
            found = reinterpret_cast<uintptr_t>(at + i);
            count++;
        }
    }
    return count == 1 ? found : 0;
}
#endif

inline geode::Mod* conflictingMod(uintptr_t address) {
    for (auto* mod : geode::Loader::get()->getAllMods()) {
        if (mod == geode::Mod::get()) continue;
        for (auto* patch : mod->getPatches()) {
            if (patch->isEnabled() && patch->getAddress() == address) return mod;
        }
    }
    return nullptr;
}

// Check the game's code and create the patch (left switched off). Returns true if usable.
inline bool prepare() {
    if (g_state == State::Ready) return true;
    if (g_state == State::Unavailable) return false;
    g_state = State::Unavailable;
#if defined(VBOT_STEP_ANDROID) || defined(VBOT_STEP_WINDOWS) || defined(VBOT_STEP_MIDHOOK)
    if (auto* echo = geode::Loader::get()->getLoadedMod("decks.echo")) {
        g_why = "Conflict: decks.echo patches TPS the same way as Vbot, breaking it.";
        g_whyIsFullMessage = true;
        return false;
    }

#ifdef VBOT_STEP_MIDHOOK
    if (auto* xenon = geode::Loader::get()->getLoadedMod("baleszka.xenon"); xenon && xenon->isLoaded()) {
        g_why = "Conflict: Xenon changes TPS at the same spots as Vbot, breaking it.";
        g_whyIsFullMessage = true;
        return false;
    }
    {
        uintptr_t base = geode::base::get();
        g_hooks.push_back(safetyhook::create_mid(base + HOOK_PHYS_DT, midPhysDt));
        g_hooks.push_back(safetyhook::create_mid(base + HOOK_STEP_COUNT, midStepCount));
        g_hooks.push_back(safetyhook::create_mid(base + HOOK_RESTORE_DT, midRestoreDt));
        geode::log::info("Vbot step hooks installed at +{:#x}, +{:#x}, +{:#x}", HOOK_PHYS_DT, HOOK_STEP_COUNT, HOOK_RESTORE_DT);
        g_state = State::Ready;
        return true;
    }
#else
    uintptr_t where = geode::base::get() + OFFSET;
    auto* ptr = &g_count;
    static_assert(sizeof(ptr) == 8, "the patch holds a 64-bit address");

    if (auto* other = conflictingMod(where)) {
        g_why = "the mod " + other->getName() + " already changed the same part of the game, turn it off";
        return false;
    }

    std::vector<uint8_t> bytes;
#ifdef VBOT_STEP_ANDROID
    where = findCountMath();
    if (where == 0) {
        auto* at = reinterpret_cast<const uint32_t*>(geode::base::get() + OFFSET);
        std::string found;
        for (size_t i = 0; i < ORIGINAL.size(); i++) found += fmt::format("{:08x} ", at[i]);
        geode::log::warn("Vbot step patch: expected the count math at +{:#x}, found: {}", OFFSET, found);
        g_why = "the game's code is different from the version Vbot was built for (see the Geode log)";
        return false;
    }
    if (where != geode::base::get() + OFFSET) {
        geode::log::info("Vbot step patch: count math found at +{:#x} instead of +{:#x}", where - geode::base::get(), OFFSET);
    }
    auto* address = reinterpret_cast<uint8_t*>(where);
    bytes.resize(sizeof(REPLACEMENT) + sizeof(ptr));
    std::memcpy(bytes.data(), REPLACEMENT.data(), sizeof(REPLACEMENT));
    std::memcpy(bytes.data() + sizeof(REPLACEMENT), &ptr, sizeof(ptr));
#else
    auto* address = reinterpret_cast<uint8_t*>(where);
    bytes.assign(WIN_REPLACEMENT.begin(), WIN_REPLACEMENT.end());
    std::memcpy(bytes.data() + 2, &ptr, sizeof(ptr));
#endif

    auto res = geode::Mod::get()->patch(address, bytes);
    if (!res) {
        g_why = "could not write the patch";
        return false;
    }
    g_patch = res.unwrap();
    g_on = true; // a new patch starts switched on
    (void)g_patch->disable();
    g_on = false;
    g_state = State::Ready;
    return true;
#endif // VBOT_STEP_MIDHOOK
#else
    g_why = "not available for this platform and game version";
    return false;
#endif
}

// Switch the patch on or off. Does nothing if it isn't ready or is already in that state.
inline void setEnabled(bool on) {
#ifdef VBOT_STEP_MIDHOOK
    if (g_state == State::Ready) g_on = on; // the hooks stay installed; they only act while this is set
    return;
#endif
    if (g_state != State::Ready || !g_patch || g_on == on) return;
    auto res = on ? g_patch->enable() : g_patch->disable();
    if (res) g_on = on;
}

} // namespace vbot::stepPatch
