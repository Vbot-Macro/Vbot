#pragma once
#include <Geode/Geode.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace vbot::hb {

enum class Kind { None, Hazard, Orb, Block, Passable };

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
            return Kind::Orb;
    }
}

struct Settings {
    bool fill = false;
    bool playerRotation = true;
    bool player = true;
    bool showActivated = true;
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

constexpr float LINE = 0.5f;
constexpr float FILL_ALPHA = 0.3f;
constexpr float PLAYER_INNER = 0.25f;

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
    auto* obj = static_cast<GameObject*>(p);
    auto outer = obj->getObjectRect();
    cocos2d::CCRect inner(outer.getMidX() - outer.size.width * PLAYER_INNER / 2.f,
                          outer.getMidY() - outer.size.height * PLAYER_INNER / 2.f,
                          outer.size.width * PLAYER_INNER, outer.size.height * PLAYER_INNER);
    rect(n, outer, st.hazard, st.fill);
    rect(n, inner, st.block, st.fill);
    if (st.playerRotation) {
        if (auto* box = p->getOrientedBox()) {
            cocos2d::CCPoint pts[4] = {box->m_corners[0], box->m_corners[1], box->m_corners[2], box->m_corners[3]};
            polygon(n, pts, 4, st.hazard, false, 0.6f);
        }
    }
}

}

inline void draw(GJBaseGameLayer* gl, cocos2d::CCDrawNode* node) {
    if (!gl || !node) return;
    auto& st = Settings::get();
    node->clear();

    auto const& columns = gl->m_sections;
    int columnCount = static_cast<int>(columns.size());
    int left = std::max(0, gl->m_leftSectionIndex);
    int right = std::min(gl->m_rightSectionIndex, columnCount - 1);

    std::vector<GameObject*> visible;
    for (int x = left; x <= right; x++) {
        auto* column = columns[x];
        if (!column) continue;
        int cellCount = static_cast<int>(column->size());
        int bottom = std::max(0, gl->m_bottomSectionIndex);
        int top = std::min(gl->m_topSectionIndex, cellCount - 1);
        for (int y = bottom; y <= top; y++) {
            auto* cell = (*column)[y];
            if (!cell) continue;
            for (auto* obj : *cell) {
                if (obj) visible.push_back(obj);
            }
        }
    }

    std::sort(visible.begin(), visible.end());
    visible.erase(std::unique(visible.begin(), visible.end()), visible.end());
    for (auto* obj : visible) detail::drawObject(node, obj, st);

    if (st.player) {
        detail::drawPlayer(node, gl->m_player1, st);
        if (gl->m_gameState.m_isDualMode && gl->m_player2 && gl->m_player2->isVisible()) {
            detail::drawPlayer(node, gl->m_player2, st);
        }
    }
}

}
