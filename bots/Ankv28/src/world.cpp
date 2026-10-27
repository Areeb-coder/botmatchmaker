#include "world.h"
#include <algorithm>
#include <variant>

using cambc::camp_type;

namespace {

// Planning health of a gem or gold cluster (true health 120); tuned.
constexpr float cluster_effective_health = 500.0f;

// Camp stats (week 2 patch): health and damage per turn while awake.
float camp_health(camp_type kind) {
    switch (kind) {
    case camp_type::wolf: return 1350;
    case camp_type::elk: return 2400;
    case camp_type::golem: return 5000;
    case camp_type::monolith: return 20000;
    // Gem and gold clusters have 120 health since the week 2 patch, but planning with that made v8
    // over-value cluster groups (gold-rush 1/8 vs v7): each cluster costs walking and a separate attack.
    default: return cluster_effective_health;
    }
}

float camp_damage(camp_type kind) {
    switch (kind) {
    case camp_type::wolf: return 20;
    case camp_type::elk: return 60;
    case camp_type::golem: return 120;
    case camp_type::monolith: return 200;
    default: return 0;
    }
}

} // namespace

int camp_range(camp_type kind) {
    switch (kind) {
    case camp_type::elk:
    case camp_type::golem: return 4;
    case camp_type::monolith: return 5;
    default: return 3;
    }
}

void world::init() {
    width_ = cambc::map::width();
    group_at_.assign(width_ * cambc::map::height(), -1);
    all_tiles_.clear();
    for (auto& group : cambc::map::camp_groups()) {
        group_info info{.group = group};
        info.gems = static_cast<int>(group.gem_value());
        info.gold = static_cast<int>(group.gold_value());
        info.gold_only = info.gems == 0;
        info.tiles.clear();
        std::vector<std::pair<float, float>> fighters;  // (health, damage) of members that hit back
        for (const auto& member : group) {
            const auto kind = member.kind();
            info.monolith |= kind == camp_type::monolith;
            info.mobs |= kind == camp_type::wolf || kind == camp_type::elk || kind == camp_type::golem;
            info.gold_only &= kind == camp_type::gold_cluster;
            info.health += camp_health(kind);
            info.damage += camp_damage(kind);
            if (camp_damage(kind) > 0)
                fighters.emplace_back(camp_health(kind), camp_damage(kind));
            const auto p = member.spawn_position();
            info.tiles.set(p);
            group_at_[p.y * width_ + p.x] = static_cast<std::int16_t>(groups_.size());
        }
        // Kill the most damage per health first (Smith's rule) to minimise damage taken.
        std::sort(fighters.begin(), fighters.end(),
                  [](const auto& a, const auto& b) { return a.second / a.first > b.second / b.first; });
        float alive_damage = info.damage;
        for (const auto& [health, damage] : fighters) {
            info.exposure += health * alive_damage;
            alive_damage -= damage;
        }
        info.anchor = group.camps().front().spawn_position();
        info.remaining = info.health;
        all_tiles_ |= info.tiles;
        groups_.push_back(std::move(info));
    }
}

const cambc::camp* world::camp_at(cambc::vec2 p) const {
    if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y * width_ + p.x >= static_cast<int>(group_at_.size()))
        return nullptr;
    const int index = group_at_[p.y * width_ + p.x];
    if (index < 0)
        return nullptr;
    for (const auto& member : groups_[index].group.camps())
        if (member.spawn_position() == p)
            return &member;
    return nullptr;
}

void world::observe(int round) {
    const auto seen = cambc::vision::visible_tiles() & all_tiles_;
    if (seen.is_empty())
        return;
    int last = -1;
    for (const auto p : seen.positions()) {
        const int index = group_at_[p.y * width_ + p.x];
        if (index < 0 || index == last)
            continue;
        last = index;
        auto& info = groups_[index];
        if (info.seen_round == round)
            continue;
        info.seen_round = round;
        try {
            const auto state = info.group.state();
            info.reward_ready = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
            const auto* respawning = std::get_if<cambc::map::camp_group_respawning>(&state);
            if (respawning)
                info.respawn_round = round + respawning->timer;
            else
                info.respawn_round = 0;
            // Monolith groups: re-read the living members' health when the whole group is in sight.
            if (info.monolith || info.mobs) {  // v7: every group that fights back
                if (respawning || info.reward_ready) {
                    info.remaining = info.health;  // the next cycle starts full
                } else if ((info.tiles & ~seen).is_empty()) {
                    float left = 0;
                    for (const auto& camp : info.group.camps())
                        if (cambc::vision::has_camp(camp.spawn_position())) left += camp.health();
                    info.remaining = left;
                }
            }
        } catch (const cambc::error&) {
            // Vision edge cases: keep the previous knowledge.
        }
    }
}
