// Runs inside the first draft turn's fuel. Running out of fuel before picking randomises every
// remaining pick, so per-tile work uses flat arrays rather than SDK calls (expanse 80x80: ~210k of
// the 1M first-turn budget).
#include "map_profile.h"
#include <algorithm>
#include <cmath>
#include <vector>

using cambc::camp_type;
using cambc::team;
using cambc::vec2;

namespace {

constexpr int unreachable = cambc::distance_field::unreachable;

struct camp_point {
    vec2 position;
    float value;  // gems + gold / 4, shared evenly across the group's members
};

std::uint8_t to_byte(float share) {
    return static_cast<std::uint8_t>(std::clamp(share, 0.f, 1.f) * 255.f + 0.5f);
}

std::uint16_t to_u16(float value) {
    return static_cast<std::uint16_t>(std::clamp(value, 0.f, 65535.f));
}

// Row-major walking distances from a team's spawn; walls, holes, altars and the other spawn block.
struct walk_grid {
    int width, height;
    std::vector<std::uint16_t> distance;

    walk_grid(team side) : width{cambc::map::width()}, height{cambc::map::height()} {
        const auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(side == team::ally ? team::enemy : team::ally);
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute(blocked, cambc::map::spawn_region(side), visited);
        distance = field.export_linear_buffer();
    }

    // Nearest distance to a tile within Chebyshev 1 of `p` (camp tiles are occupied, spawns blocked).
    int next_to(vec2 p) const {
        int best = unreachable;
        for (int y = std::max(0, p.y - 1); y <= std::min(height - 1, p.y + 1); ++y)
            for (int x = std::max(0, p.x - 1); x <= std::min(width - 1, p.x + 1); ++x)
                best = std::min<int>(best, distance[y * width + x]);
        return best;
    }

    int next_to(const cambc::bitboard& tiles) const {
        int best = unreachable;
        for (const auto p : tiles.positions())
            best = std::min(best, next_to(p));
        return best;
    }
};

} // namespace

map_profile compute_map_profile() {
    map_profile profile;
    profile.team_size = static_cast<int>(cambc::draft::team_size());
    profile.width = cambc::map::width();
    profile.height = cambc::map::height();
    profile.altars = static_cast<int>(cambc::map::altars().count());

    const walk_grid ours{team::ally}, theirs{team::enemy};
    profile.spawn_distance = ours.next_to(cambc::map::spawn_region(team::enemy));

    std::vector<camp_point> points;
    float weighted_distance = 0, contested_gems = 0;
    for (const auto& group : cambc::map::camp_groups()) {
        const int gems = static_cast<int>(group.gem_value());
        const int gold = static_cast<int>(group.gold_value());
        profile.total_gems += gems;
        profile.total_gold += gold;

        bool monolith = false, elk = false, attacks = false;
        int d_ours = unreachable, d_theirs = unreachable;
        for (const auto& member : group) {
            const auto kind = member.kind();
            monolith |= kind == camp_type::monolith;
            elk |= kind == camp_type::elk;
            attacks |= kind != camp_type::gem_cluster && kind != camp_type::gold_cluster;
            d_ours = std::min(d_ours, ours.next_to(member.spawn_position()));
            d_theirs = std::min(d_theirs, theirs.next_to(member.spawn_position()));
        }
        if (monolith) profile.boss_gems += gems;
        else if (elk) profile.elk_gems += gems;
        if (!attacks) profile.free_gems += gems;

        // Monoliths are fought with everything regardless; splash value only counts farmable camps.
        if (!monolith) {
            const float each = (gems + gold / 4.f) / group.camps().size();
            for (const auto& member : group)
                points.push_back({member.spawn_position(), each});
        }

        if (d_ours == unreachable || d_theirs == unreachable)
            continue;
        weighted_distance += float(d_ours) * gems;
        if (std::abs(d_ours - d_theirs) <= 0.25f * std::max(d_ours, d_theirs))
            contested_gems += gems;
    }

    // Multi-target reach: does another farmable camp sit close enough to share a hit?
    std::vector<std::uint8_t> occupied(profile.width * profile.height, 0);
    for (const auto& point : points)
        occupied[point.position.y * profile.width + point.position.x] = 1;
    float total_value = 0, splash_value = 0, aoe_value = 0;
    for (const auto& point : points) {
        int nearest = 3;  // Chebyshev distance to the closest other camp, capped at 3
        const auto [px, py] = point.position;
        for (int y = std::max(0, py - 2); y <= std::min(profile.height - 1, py + 2); ++y)
            for (int x = std::max(0, px - 2); x <= std::min(profile.width - 1, px + 2); ++x)
                if (occupied[y * profile.width + x] && (x != px || y != py))
                    nearest = std::min(nearest, std::max(std::abs(x - px), std::abs(y - py)));
        total_value += point.value;
        if (nearest <= 1) splash_value += point.value;
        if (nearest <= 2) aoe_value += point.value;
    }
    if (total_value > 0) {
        profile.splash_share = splash_value / total_value;
        profile.aoe_share = aoe_value / total_value;
    }
    if (profile.total_gems > 0) {
        profile.camp_distance = weighted_distance / profile.total_gems;
        profile.contested_share = contested_gems / profile.total_gems;
    }
    return profile;
}

plan_features map_profile::to_features() const {
    return {
        .free_share = to_byte(share(free_gems)),
        .boss_share = to_byte(share(boss_gems)),
        .elk_share = to_byte(share(elk_gems)),
        .splash_share = to_byte(splash_share),
        .aoe_share = to_byte(aoe_share),
        .contested_share = to_byte(contested_share),
        .total_gems = to_u16(total_gems),
        .total_gold = to_u16(total_gold),
        .spawn_distance = to_u16(spawn_distance),
        .camp_distance = to_u16(camp_distance),
    };
}
