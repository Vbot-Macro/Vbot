// hitboxes.hpp - Vbot's own hitbox drawing, with colours you can change.
//
// What gets drawn (always, in any mode, whenever the Hitboxes toggle is on):
//   hazards (spikes, saws, ...)      red
//   orbs, pads, portals, coins       green
//   solid blocks and slopes          blue
//   passable (pass-through) objects  cyan
//   the player                       red box (the hazard box) + blue box (the block hitbox),
//                                    plus the rotated box when "Player Rotation" is on
// Decoration, triggers and other objects that can't be touched aren't drawn.
//
// Settings are saved between sessions. The colours are changed from the "+" button next to the
// Hitboxes toggle on the Cheats tab.
#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace vbot::hb {

// What kind of thing an object is, for colouring.
enum class Kind { None, Hazard, Orb, Block, Passable };

// Which colour an object gets. `passable` is the object's own "pass-through" flag; it wins over the type.
inline Kind kindOf(GameObjectType type, bool passable) {
    switch (type) {
        case GameObjectType::Decoration:
        case GameObjectType::Modifier:
        case GameObjectType::Special:
        case GameObjectType::EnterEffectObject:
            return Kind::None;
        default: break;
    }
    if (passable) return Kind::Passable;
    switch (type) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            return Kind::Hazard;
        case GameObjectType::Solid:
        case GameObjectType::Slope:
            return Kind::Block;
        case GameObjectType::Breakable:
        case GameObjectType::CollisionObject:
            return Kind::Block;
        default:
            return Kind::Orb; // orbs, pads, portals, coins and other things the player touches
    }
}

struct Settings {
    bool fill = false;          // fill the boxes, not only their outlines
    bool playerRotation = true; // also draw the player's rotated box
    bool player = true;         // draw the player's boxes
    bool showActivated = true;  // orbs that have been used are drawn filled so you can see them
    cocos2d::ccColor3B hazard{255, 0, 0};
    cocos2d::ccColor3B orb{0, 255, 0};
    cocos2d::ccColor3B block{0, 0, 255};
    cocos2d::ccColor3B passable{0, 255, 255};

    static Settings& get() {
        static Settings s = load();
        return s;
    }

    void save() const {
        auto* mod = geode::Mod::get();
        mod->setSavedValue<bool>("hb-fill", fill);
        mod->setSavedValue<bool>("hb-player-rotation", playerRotation);
        mod->setSavedValue<bool>("hb-player", player);
        mod->setSavedValue<bool>("hb-show-activated", showActivated);
        mod->setSavedValue<int>("hb-hazard", pack(hazard));
        mod->setSavedValue<int>("hb-orb", pack(orb));
        mod->setSavedValue<int>("hb-block", pack(block));
        mod->setSavedValue<int>("hb-passable", pack(passable));
    }

    cocos2d::ccColor3B& colorOf(Kind k) {
        switch (k) {
            case Kind::Hazard: return hazard;
            case Kind::Orb: return orb;
            case Kind::Passable: return passable;
            default: return block;
        }
    }

private:
    static int pack(cocos2d::ccColor3B c) { return (c.r << 16) | (c.g << 8) | c.b; }
    static cocos2d::ccColor3B unpack(int v, cocos2d::ccColor3B fallback) {
        if (v < 0 || v > 0xFFFFFF) return fallback;
        return {static_cast<GLubyte>(v >> 16), static_cast<GLubyte>(v >> 8), static_cast<GLubyte>(v)};
    }
    static Settings load() {
        auto* mod = geode::Mod::get();
        Settings s;
        s.fill = mod->getSavedValue<bool>("hb-fill", s.fill);
        s.playerRotation = mod->getSavedValue<bool>("hb-player-rotation", s.playerRotation);
        s.player = mod->getSavedValue<bool>("hb-player", s.player);
        s.showActivated = mod->getSavedValue<bool>("hb-show-activated", s.showActivated);
        s.hazard = unpack(mod->getSavedValue<int>("hb-hazard", pack(s.hazard)), s.hazard);
        s.orb = unpack(mod->getSavedValue<int>("hb-orb", pack(s.orb)), s.orb);
        s.block = unpack(mod->getSavedValue<int>("hb-block", pack(s.block)), s.block);
        s.passable = unpack(mod->getSavedValue<int>("hb-passable", pack(s.passable)), s.passable);
        return s;
    }
};

namespace detail {

constexpr float LINE = 0.5f;          // outline width
constexpr float FILL_ALPHA = 0.3f;    // fill strength when "Fill Hitboxes" is on
constexpr float PLAYER_INNER = 0.5f;  // the red player box, as a share of the block hitbox's size

inline cocos2d::ccColor4F rgba(cocos2d::ccColor3B c, float a = 1.f) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a};
}

inline void polygon(cocos2d::CCDrawNode* n, cocos2d::CCPoint* pts, unsigned count, cocos2d::ccColor3B color,
                    bool filled, float lineAlpha = 1.f) {
    n->drawPolygon(pts, count, rgba(color, filled ? FILL_ALPHA : 0.f), LINE, rgba(color, lineAlpha));
}

inline void rect(cocos2d::CCDrawNode* n, cocos2d::CCRect const& r, cocos2d::ccColor3B color, bool filled,
                 float lineAlpha = 1.f) {
    cocos2d::CCPoint pts[4] = {
        {r.getMinX(), r.getMinY()}, {r.getMinX(), r.getMaxY()},
        {r.getMaxX(), r.getMaxY()}, {r.getMaxX(), r.getMinY()},
    };
    polygon(n, pts, 4, color, filled, lineAlpha);
}

inline bool isActivated(GameObject* obj) {
    if (auto* e = geode::cast::typeinfo_cast<EnhancedGameObject*>(obj)) return e->hasBeenActivated();
    return false;
}

inline void drawObject(cocos2d::CCDrawNode* n, GameObject* obj, Settings& st) {
    Kind kind = kindOf(obj->m_objectType, obj->m_isPassable);
    if (kind == Kind::None) return;
    auto color = st.colorOf(kind);
    bool filled = st.fill || (kind == Kind::Orb && st.showActivated && isActivated(obj));

    auto const& r = obj->getObjectRect();
    float radius = obj->m_objectRadius;
    if (radius > 0.f) {
        cocos2d::CCPoint center{r.getMidX(), r.getMidY()};
        n->drawCircle(center, radius, rgba(color, filled ? FILL_ALPHA : 0.f), LINE, rgba(color), 24);
        return;
    }

    // An object turned by something other than a multiple of 90 degrees has a rotated box.
    float rot = std::fmod(std::abs(obj->getRotation()), 90.f);
    if (rot > 0.01f && rot < 89.99f) {
        if (auto* box = obj->getOrientedBox()) {
            cocos2d::CCPoint pts[4] = {box->m_corners[0], box->m_corners[1], box->m_corners[2], box->m_corners[3]};
            polygon(n, pts, 4, color, filled);
            return;
        }
    }
    rect(n, r, color, filled);
}

inline void drawPlayer(cocos2d::CCDrawNode* n, PlayerObject* p, Settings& st) {
    if (!p) return;
    auto* obj = static_cast<GameObject*>(p); // the plain GameObject versions of these functions
    auto block = obj->getObjectRect();
    // The blue box is the one the player lands on blocks with; the red one is what hazards hurt.
    rect(n, block, st.block, st.fill);
    rect(n, obj->getObjectRect(block.size.width * PLAYER_INNER, block.size.height * PLAYER_INNER), st.hazard, st.fill);
    if (st.playerRotation) {
        if (auto* box = p->getOrientedBox()) {
            cocos2d::CCPoint pts[4] = {box->m_corners[0], box->m_corners[1], box->m_corners[2], box->m_corners[3]};
            polygon(n, pts, 4, st.hazard, false, 0.6f);
        }
    }
}

} // namespace detail

// Redraws every hitbox. `node` is the game's debug draw node (a child of the object layer).
inline void draw(GJBaseGameLayer* gl, cocos2d::CCDrawNode* node) {
    if (!gl || !node) return;
    auto& st = Settings::get();
    node->clear();

    for (auto* obj : gl->m_activeObjects) {
        if (!obj) continue;
        detail::drawObject(node, obj, st);
    }
    if (st.player) {
        detail::drawPlayer(node, gl->m_player1, st);
        if (gl->m_gameState.m_isDualMode) detail::drawPlayer(node, gl->m_player2, st);
    }
}

} // namespace vbot::hb
