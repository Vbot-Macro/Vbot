// vb.hpp - Vbot macro format (.vb) versions 1, 2 and 3, header-only, no dependencies.
//
// Layout (version 1, inputs only):
//   "VBOT"            4 bytes magic
//   version           1 byte
//   fps               varint
//   event count       varint
//   events            count x varint, each = (delta << 4) | (button << 2) | (down << 1) | player
//     delta  = frames since previous event (first event: since frame 0)
//     button = 0 jump, 1 left, 2 right
//     down   = 1 press, 0 release
//     player = 0 player 1, 1 player 2
//
// Version 3 (what Vbot writes when it has position data) is version 1 with a snapshot section
// added after the events:
//   snapshot count    varint
//   snapshots         count x ( delta varint, mask byte, then 12 bytes for each player in the mask )
//     delta = frames since the previous snapshot (first one: since frame 0)
//     mask  = bit 0: player 1 is included, bit 1: player 2 is included
//     per player: x, y, y-velocity as 3 little-endian float32
//   A snapshot is the state of the player(s) at the START of a physics step. Replay puts a player
//   back to it if it has drifted, which keeps a long macro on track. Vbot takes one at every
//   frame that has an input and one every 1/8 s, for both players in a 2-player level.
//
// Version 2 (older Vbot) had a flags byte after the version byte, and x, y, y-velocity of the
// pressing player after every event. Vbot still reads it and converts it to snapshots.
// Macros without any position data are written as version 1, so older Vbot versions can open them.
//
// Small deltas (< 8) fit in a single byte per input.
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace vb {

enum class Button : uint8_t { Jump = 0, Left = 1, Right = 2 };

struct Input {
    uint32_t frame = 0;
    Button button = Button::Jump;
    bool down = false;
    bool player2 = false;

    bool operator==(Input const& o) const {
        return frame == o.frame && button == o.button && down == o.down && player2 == o.player2;
    }
};

struct PlayerState {
    float x = 0.f, y = 0.f, yVel = 0.f;
    bool operator==(PlayerState const& o) const { return x == o.x && y == o.y && yVel == o.yVel; }
};

// The state of the player(s) at the start of one physics step.
struct Snapshot {
    uint32_t frame = 0;
    bool has[2] = {false, false}; // [0] = player 1, [1] = player 2
    PlayerState p[2];

    bool operator==(Snapshot const& o) const {
        for (int i = 0; i < 2; i++) {
            if (has[i] != o.has[i] || (has[i] && !(p[i] == o.p[i]))) return false;
        }
        return frame == o.frame;
    }
};

struct Macro {
    uint32_t fps = 240;
    std::vector<Input> inputs;    // must be sorted by frame
    std::vector<Snapshot> snaps;  // must be sorted by frame, one per frame at most
};

namespace detail {
inline void putVarint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

inline bool getVarint(std::vector<uint8_t> const& in, size_t& pos, uint64_t& v) {
    v = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
        if (pos >= in.size()) return false;
        uint8_t b = in[pos++];
        v |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

inline void putFloat(std::vector<uint8_t>& out, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(u >> (8 * i)));
}

inline bool getFloat(std::vector<uint8_t> const& in, size_t& pos, float& f) {
    if (in.size() - pos < 4) return false;
    uint32_t u = 0;
    for (int i = 0; i < 4; i++) u |= static_cast<uint32_t>(in[pos++]) << (8 * i);
    std::memcpy(&f, &u, 4);
    return true;
}

inline bool getState(std::vector<uint8_t> const& in, size_t& pos, PlayerState& s) {
    return getFloat(in, pos, s.x) && getFloat(in, pos, s.y) && getFloat(in, pos, s.yVel);
}

inline bool finite(PlayerState const& s) {
    return std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.yVel);
}

inline void putEvents(std::vector<uint8_t>& out, std::vector<Input> const& inputs) {
    putVarint(out, inputs.size());
    uint32_t prev = 0;
    for (auto const& i : inputs) {
        uint64_t delta = i.frame - prev;
        prev = i.frame;
        putVarint(out, (delta << 4) | (static_cast<uint64_t>(i.button) << 2) |
                           (static_cast<uint64_t>(i.down) << 1) | static_cast<uint64_t>(i.player2));
    }
}

// Adds the state of one player at a frame to a sorted snapshot list (merging into the snapshot
// that is already there for that frame, if any).
inline void addState(std::vector<Snapshot>& snaps, uint32_t frame, bool player2, PlayerState const& st) {
    if (snaps.empty() || snaps.back().frame != frame) {
        Snapshot s;
        s.frame = frame;
        snaps.push_back(s);
    }
    auto& s = snaps.back();
    s.has[player2] = true;
    s.p[player2] = st;
}
} // namespace detail

inline std::vector<uint8_t> encode(Macro const& m) {
    // Only use version 3 when there is position data.
    bool withSnaps = !m.snaps.empty();

    std::vector<uint8_t> out{'V', 'B', 'O', 'T', static_cast<uint8_t>(withSnaps ? 3 : 1)};
    detail::putVarint(out, m.fps);
    detail::putEvents(out, m.inputs);
    if (!withSnaps) return out;

    detail::putVarint(out, m.snaps.size());
    uint32_t prev = 0;
    for (auto const& s : m.snaps) {
        detail::putVarint(out, s.frame - prev);
        prev = s.frame;
        out.push_back(static_cast<uint8_t>((s.has[0] ? 1 : 0) | (s.has[1] ? 2 : 0)));
        for (int i = 0; i < 2; i++) {
            if (!s.has[i]) continue;
            detail::putFloat(out, s.p[i].x);
            detail::putFloat(out, s.p[i].y);
            detail::putFloat(out, s.p[i].yVel);
        }
    }
    return out;
}

inline std::optional<Macro> decode(std::vector<uint8_t> const& in) {
    if (in.size() < 5 || in[0] != 'V' || in[1] != 'B' || in[2] != 'O' || in[3] != 'T' ||
        (in[4] < 1 || in[4] > 3))
        return std::nullopt;
    uint8_t version = in[4];
    size_t pos = 5;
    bool v2Pos = false;
    if (version == 2) {
        if (pos >= in.size()) return std::nullopt;
        uint8_t flags = in[pos++];
        if (flags & ~1u) return std::nullopt; // unknown flags: written by a newer Vbot
        v2Pos = flags & 1;
    }
    uint64_t fps, count;
    if (!detail::getVarint(in, pos, fps) || !detail::getVarint(in, pos, count)) return std::nullopt;
    // every event takes at least 1 byte, so this rejects absurd counts early
    if (count > in.size() - pos) return std::nullopt;

    Macro m;
    m.fps = static_cast<uint32_t>(fps);
    m.inputs.reserve(count);
    uint32_t frame = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t packed;
        if (!detail::getVarint(in, pos, packed)) return std::nullopt;
        uint8_t button = (packed >> 2) & 3;
        if (button > 2) return std::nullopt;
        frame += static_cast<uint32_t>(packed >> 4);
        Input input{frame, static_cast<Button>(button), static_cast<bool>((packed >> 1) & 1),
                    static_cast<bool>(packed & 1)};
        m.inputs.push_back(input);
        if (v2Pos) {
            // Version 2: the pressing player's state follows every event. NaN x = no data.
            PlayerState st;
            if (!detail::getState(in, pos, st)) return std::nullopt;
            if (!std::isnan(st.x)) {
                if (!detail::finite(st)) return std::nullopt;
                detail::addState(m.snaps, frame, input.player2, st);
            }
        }
    }

    if (version == 3) {
        uint64_t snapCount;
        if (!detail::getVarint(in, pos, snapCount)) return std::nullopt;
        // every snapshot takes at least 2 bytes (delta + mask)
        if (snapCount > (in.size() - pos) / 2) return std::nullopt;
        m.snaps.reserve(snapCount);
        uint32_t f = 0;
        for (uint64_t n = 0; n < snapCount; n++) {
            uint64_t delta;
            if (!detail::getVarint(in, pos, delta) || pos >= in.size()) return std::nullopt;
            uint8_t mask = in[pos++];
            if (mask == 0 || (mask & ~3u)) return std::nullopt;
            f += static_cast<uint32_t>(delta);
            if (n > 0 && delta == 0) return std::nullopt; // one snapshot per frame
            Snapshot s;
            s.frame = f;
            for (int i = 0; i < 2; i++) {
                if (!(mask & (1u << i))) continue;
                s.has[i] = true;
                if (!detail::getState(in, pos, s.p[i]) || !detail::finite(s.p[i])) return std::nullopt;
            }
            m.snaps.push_back(s);
        }
    }
    return m;
}

} // namespace vb
