// PathfindingService: finds a walking route for a character, like Roblox's.
//
// The ground is looked at as a grid of 1-unit squares. Starting from where the
// character stands, it tries stepping to each of the 8 squares around it: is
// there floor there (not too far down), is it low enough to step or jump up
// to, and does the body fit (no wall)? The "A*" search tries the most
// promising squares first (the ones closest to the goal), so it finds the
// shortest route without looking everywhere.
#include "Physics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>

namespace {

constexpr float kCell      = 1.0f;
constexpr float kMaxDrop   = 6.0f;    // won't walk off anything taller than this
constexpr int   kMaxVisits = 6000;    // give up after looking at this many squares

bool overlapXZ(const AABB& a, const AABB& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.z < b.max.z && a.max.z > b.min.z;
}

} // namespace

bool Physics::findPath(const glm::vec3& start, const glm::vec3& goal, float jumpHeight,
                       std::vector<PathPoint>& out) const {
    out.clear();
    glm::vec2 flat(goal.x - start.x, goal.z - start.z);
    const float dist = glm::length(flat);
    const int radius = (int)std::ceil(dist / kCell) + 16;
    if (radius > 120) return false;   // too far to search: walk straight or split the trip

    // Only the solid parts near the route matter.
    AABB region{glm::min(start, goal) - glm::vec3(radius * kCell, kMaxDrop + 4.0f, radius * kCell),
                glm::max(start, goal) + glm::vec3(radius * kCell, 12.0f, radius * kCell)};
    std::vector<const Collider*> near;
    for (const auto& c : m_colliders)
        if (c.solid && c.box.overlaps(region)) near.push_back(&c);

    // Does a character standing at `f` fit (nothing solid in its body)?
    auto fits = [&](const glm::vec3& f) {
        AABB body = characterBox(f + glm::vec3(0.0f, 0.05f, 0.0f));
        body.min.x += 0.05f; body.min.z += 0.05f; body.max.x -= 0.05f; body.max.z -= 0.05f;
        OBB ob = OBB::fromAABB(body);
        for (const Collider* c : near) {
            if (!body.overlaps(c->box)) continue;
            if (!c->rotated) return false;
            glm::vec3 n; float d;
            if (obbOverlap(ob, c->obb, n, d)) return false;
        }
        return true;
    };
    // The floor under `f`: the highest top from `up` above to kMaxDrop below.
    auto ground = [&](const glm::vec3& f, float up, float& y) {
        AABB foot{{f.x - 0.35f, 0, f.z - 0.35f}, {f.x + 0.35f, 0, f.z + 0.35f}};
        float best = -1e9f;
        for (const Collider* c : near) {
            if (!overlapXZ(foot, c->box)) continue;
            float top = c->box.max.y;
            if (top <= f.y + up && top >= f.y - kMaxDrop) best = std::max(best, top);
        }
        if (best < -1e8f) return false;
        y = best;
        return true;
    };

    struct Node { glm::vec3 pos; float g; int64_t parent; bool jump; bool closed; };
    std::unordered_map<int64_t, Node> nodes;
    const int sx = (int)std::lround(start.x / kCell), sz = (int)std::lround(start.z / kCell);
    const int gx = (int)std::lround(goal.x / kCell),  gz = (int)std::lround(goal.z / kCell);
    auto key = [&](int ix, int iz, float y) -> int64_t {
        int64_t a = ix - sx + 512, b = iz - sz + 512, l = (int64_t)std::lround(y * 2.0f) + 4096;
        return (a << 40) | (b << 20) | l;
    };
    auto cellOf = [](const glm::vec3& p, int& ix, int& iz) {
        ix = (int)std::lround(p.x / kCell); iz = (int)std::lround(p.z / kCell);
    };

    float goalY = goal.y;
    ground(goal, 1.0f, goalY);
    float startY = start.y;
    ground(start, 0.6f, startY);
    glm::vec3 s(start.x, startY, start.z);

    using Item = std::pair<float, int64_t>;   // (estimated total, node)
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int64_t startKey = key(sx, sz, startY);
    nodes[startKey] = {s, 0.0f, -1, false, false};
    open.push({dist, startKey});

    int64_t found = -1;
    int visits = 0;
    while (!open.empty() && visits < kMaxVisits) {
        int64_t k = open.top().second;
        open.pop();
        Node cur = nodes[k];
        if (cur.closed) continue;
        nodes[k].closed = true;
        ++visits;

        int cx, cz;
        cellOf(cur.pos, cx, cz);
        if (std::abs(cx - gx) <= 1 && std::abs(cz - gz) <= 1 && std::abs(cur.pos.y - goalY) < 2.5f) { found = k; break; }

        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) {
                if (!dx && !dz) continue;
                int nx = cx + dx, nz = cz + dz;
                if (std::abs(nx - sx) > radius || std::abs(nz - sz) > radius) continue;
                glm::vec3 np(nx * kCell, cur.pos.y, nz * kCell);
                float ny;
                if (!ground(np, jumpHeight, ny)) continue;
                np.y = ny;
                const float rise = ny - cur.pos.y;
                const bool jump = rise > kStepHeight;
                if (!fits(np)) continue;
                // Room to jump up here, and no cutting across a wall's corner.
                if (jump && !fits(glm::vec3(cur.pos.x, ny, cur.pos.z))) continue;
                if (dx && dz && (!fits(glm::vec3(cur.pos.x + dx * kCell, std::max(ny, cur.pos.y), cur.pos.z)) ||
                                 !fits(glm::vec3(cur.pos.x, std::max(ny, cur.pos.y), cur.pos.z + dz * kCell))))
                    continue;
                const int64_t nk = key(nx, nz, ny);
                const float g = cur.g + ((dx && dz) ? 1.414f : 1.0f) * kCell + (jump ? 1.5f : 0.0f) +
                                (rise < -1.0f ? 0.5f : 0.0f);
                auto it = nodes.find(nk);
                if (it != nodes.end() && (it->second.closed || it->second.g <= g)) continue;
                nodes[nk] = {np, g, k, jump, false};
                float h = glm::length(glm::vec2(np.x - goal.x, np.z - goal.z)) + std::abs(ny - goalY) * 0.5f;
                open.push({g + h, nk});
            }
    }
    if (found < 0) return false;

    // Walk back from the goal to the start.
    std::vector<PathPoint> raw;
    for (int64_t k = found; k >= 0; k = nodes[k].parent) raw.push_back({nodes[k].pos, nodes[k].jump});
    std::reverse(raw.begin(), raw.end());
    raw.front().pos = s;
    // The exact goal at the end, if a body fits there.
    glm::vec3 end(goal.x, goalY, goal.z);
    if (glm::length(glm::vec2(end.x - raw.back().pos.x, end.z - raw.back().pos.z)) > 0.05f && fits(end))
        raw.push_back({end, false});

    // Keep only the corners (and the jumps): straight runs become one waypoint.
    out.push_back(raw.front());
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        glm::vec3 a = raw[i].pos - raw[i - 1].pos, b = raw[i + 1].pos - raw[i].pos;
        bool turn = std::abs(a.x - b.x) > 0.01f || std::abs(a.z - b.z) > 0.01f;
        bool climb = std::abs(a.y) > 0.3f || std::abs(b.y) > 0.3f;
        if (turn || climb || raw[i].jump || raw[i + 1].jump) out.push_back(raw[i]);
    }
    if (raw.size() > 1) out.push_back(raw.back());
    return true;
}
