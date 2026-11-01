// step_patch.hpp - lets the TPS bypass change how many physics steps the game runs per frame.
//
// What the game does: inside GJBaseGameLayer::update it turns the frame time into a step count,
//     count = round(delta * 60 / timeWarp * 4)        (that is: delta * 240 / timeWarp)
// and then splits the time into that many equal steps. The "240" is baked into that math, so
// no hook can make the game take more steps. This file replaces just that one calculation with
// "read the count from a variable of ours", so the TPS bypass can say how many steps to run.
//
// It is a raw byte patch, not a hook: a hook would have to save and restore the CPU registers
// around our code, and that changes the numbers the game keeps in them for the steps after it.
//
// Safety:
//  - On Android the bytes are only replaced if the original instructions match exactly what we
//    expect. On Windows the original bytes aren't known, so the mod itself only loads on game
//    version 2.2081. In both cases the patch is refused if another mod patches the same spot.
//    If it is refused, nothing is touched and the TPS bypass stays off.
//  - The patch is switched on only for the frames that need it and off again otherwise, so
//    with the bypass off the game runs its own code.
//
// Supported: Android 64-bit and Windows, game version 2.2081 only (a fixed spot in that version's
// code). Everywhere else this reports "not available".
//
// Credit: the Windows patch location and bytes come from Echo (https://github.com/lcm7341/echo-beta,
// GPL-3.0), who took them from Silicate (GPL-3.0). Vbot is GPL-3.0 as well. The Android patch was
// worked out from the arm64 disassembly of the same function.
#pragma once
#include <Geode/Geode.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vbot::stepPatch {

// The step count the patched game code reads each frame. Written by our update() hook.
inline uint32_t g_count = 1;

#if defined(GEODE_IS_ANDROID64) || defined(GEODE_IS_WINDOWS)
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

#ifdef GEODE_IS_ANDROID64
// Where the count math sits in GJBaseGameLayer::update (offset into the game library, 2.2081).
inline constexpr uintptr_t OFFSET = 0x89d9f8;

// The ten instructions we expect to find there (arm64), in the order they run:
//   fmov d10, #4.0              ; the "* 4"
//   fmul d10, d11, d10          ; d11 = delta * 60 / timeWarp
//   fcvt s0, d10
//   frinta s9, s0               ; round to the nearest whole number
//   fmov s0, #1.0
//   fcmpe s9, s0
//   b.le <the "one step" path>
//   fcvtzs w0, s9               ; w0 = step count
//   scvtf d0, w0                ; d0 = the count as a double
//   str w0, [x29, #224]         ; the count is also kept on the stack
inline constexpr std::array<uint32_t, 10> ORIGINAL = {
    0x1e62100a, 0x1e6a096a, 0x1e624140, 0x1e264009, 0x1e2e1000,
    0x1e202130, 0x5400758d, 0x1e380120, 0x1e620000, 0xb900e3a0,
};

// Our replacement. It ends in the same state as the original at the same spot:
// w0 = count (at least 1), d0 = count as a double, count stored at [x29, #224].
//   ldr  x0, <address of g_count>     ; the address sits in the last two words below
//   ldr  w0, [x0]
//   cmp  w0, #1
//   csinc w0, w0, wzr, ge             ; w0 = (w0 >= 1) ? w0 : 1
//   scvtf d0, w0
//   str  w0, [x29, #224]
//   b    <first instruction after the original ten>
//   nop
//   <8 bytes: pointer to g_count>
inline constexpr std::array<uint32_t, 8> REPLACEMENT = {
    0x58000100, 0xb9400000, 0x7100041f, 0x1a9fa400,
    0x1e620000, 0xb900e3a0, 0x14000004, 0xd503201f,
};
#endif

#ifdef GEODE_IS_WINDOWS
// Where the count math sits in GJBaseGameLayer::update (offset into GeometryDash.exe, 2.2081).
inline constexpr uintptr_t OFFSET = 0x237A5B;
// Replacement (x86-64): load our count into r11d and jump over the game's own count math.
//   movabs r11, <address of g_count>   ; 49 BB + 8 bytes
//   mov    r11d, [r11]                 ; 45 8B 1B
//   jmp    +0x31                       ; E9 31 00 00 00
// A raw patch rather than a hook, because a hook saves every register and that changes the
// ship's step. The original bytes at this spot aren't checked (we only have the location), so the
// mod only loads on game version 2.2081 and the patch is refused if another mod patches the same spot.
inline constexpr std::array<uint8_t, 18> WIN_REPLACEMENT = {
    0x49, 0xBB, 0, 0, 0, 0, 0, 0, 0, 0,
    0x45, 0x8B, 0x1B,
    0xE9, 0x31, 0x00, 0x00, 0x00,
};
#endif

#ifdef GEODE_IS_ANDROID64
// Find the count math. Normally it is exactly at OFFSET. If the library is a slightly different build
// of the same code, the instructions can sit a little further along, so look nearby for the same ones.
// Every word has to match except the b.le (its jump distance is different in every build, but it must
// still be a "branch if less or equal"), and there has to be exactly one match.
inline uintptr_t findCountMath() {
    uintptr_t base = geode::base::get();
    auto* at = reinterpret_cast<const uint32_t*>(base + OFFSET);
    if (std::memcmp(at, ORIGINAL.data(), sizeof(ORIGINAL)) == 0) return base + OFFSET;

    auto matches = [](const uint32_t* p) {
        for (size_t i = 0; i < ORIGINAL.size(); i++) {
            if (i == 6) {
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

// Another mod patching the same bytes (Silicate does) would fight with us over them.
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
#if defined(GEODE_IS_ANDROID64) || defined(GEODE_IS_WINDOWS)
    // Echo patches the TPS code the same way Vbot does, so the two can't run together.
    if (auto* echo = geode::Loader::get()->getLoadedMod("decks.echo"); echo && echo->isEnabled()) {
        g_why = "Conflict: decks.echo patches TPS the same way as Vbot, breaking it.";
        g_whyIsFullMessage = true;
        return false;
    }

    uintptr_t where = geode::base::get() + OFFSET;
    auto* ptr = &g_count;
    static_assert(sizeof(ptr) == 8, "the patch holds a 64-bit address");

    // Another mod that already changed this spot (Echo and Silicate do) is the most likely reason the
    // original instructions aren't there, so check for that first.
    if (auto* other = conflictingMod(where)) {
        g_why = "the mod " + other->getName() + " already changed the same part of the game, turn it off";
        return false;
    }

    std::vector<uint8_t> bytes;
#ifdef GEODE_IS_ANDROID64
    where = findCountMath();
    if (where == 0) {
        auto* at = reinterpret_cast<const uint32_t*>(geode::base::get() + OFFSET);
        std::string found;
        for (size_t i = 0; i < ORIGINAL.size(); i++) found += fmt::format("{:08x} ", at[i]);
        geode::log::warn("Vbot step patch: expected the count math at +{:#x}, found: {}", OFFSET, found);
        g_why = "the game's code is different from 2.2081 (see the Geode log)";
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
#else
    g_why = "only available on Android 64-bit and Windows";
    return false;
#endif
}

// Switch the patch on or off. Does nothing if it isn't ready or is already in that state.
inline void setEnabled(bool on) {
    if (g_state != State::Ready || !g_patch || g_on == on) return;
    auto res = on ? g_patch->enable() : g_patch->disable();
    if (res) g_on = on;
}

} // namespace vbot::stepPatch
