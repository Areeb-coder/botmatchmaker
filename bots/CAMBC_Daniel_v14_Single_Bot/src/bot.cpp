// Strategy v4. Every champion, every turn:
//   dead -> shop | channelling or stunned -> wait | hurt -> step out of reach, recall
//   | enemy tributing beside us -> drop an altar lock | enemies -> fight only when ahead, else fall back
//   | carrying gems -> tribute (breaking enemy altar locks) | rich -> recall to shop | else own target.
// Every body farms on its own (v3 farmed in pairs and captured half as often as the +200 bot) and
// claims its target in shared memory so allies pick other groups; bodies share a group when it
// needs them or beats their other options. Shared memory also holds the last place we were the
// weaker side, which everybody avoids for a while.
#include <cambc.h>
#include "bot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <span>
#include <variant>
#include <vector>

using cambc::team;
using cambc::vec2;

namespace {

// Shared memory is one little-endian 64-bit word of bit fields:
//   bits 0-35   a claim per body (6 bits each): target group index + 1, 0 = none
//   bits 36-42  danger x + 1 (0 = none), bits 43-49 danger y, bits 50-57 its round mod 256
// The largest map has 42 camp groups, so 6-bit claims fit; groups past index 62 go unclaimed.
constexpr int claim_bits = 6, max_bodies = 6;
constexpr int danger_x_bit = 36, danger_y_bit = 43, danger_round_bit = 50;
// Avoid camp groups within this many tiles of the danger marker for this many rounds. The marker
// is set where we found ourselves the weaker side.
constexpr int danger_radius = 2, danger_rounds = 20;

// Rough per-body figures for allies' kill-time and risk (measured: 35-50 damage/turn at the start,
// more once shards are bought); our own estimate is measured each turn (`measure_damage`).
constexpr float body_damage = 50, body_health = 900;
constexpr int capture_turns = 5;
// Skip monster groups whose expected damage exceeds this multiple of the bodies' health. Above 1 on
// purpose: hurt champions walk out of reach (camps do not chase or heal) and recall, so a costly
// fight costs trips home, not a death, and the target rate already charges for those trips. Lower
// values made casters (~45 damage/turn) wait for help on two wolves.
constexpr float max_damage_share = 4.0351f;
// Give up on a target after waiting this many turns for help, and ignore it for a while.
constexpr int max_wait_turns = 20, give_up_rounds = 60;
// Turns without getting closer to the target or hitting it before we give up on it.
constexpr int stall_turns = 14;
// Target rate = (gems + gold_weight * gold) per body per turn. Gems win; gold only buys power.
constexpr float gold_weight = 0.2f;
// A trip home to heal is the recall plus the walk back out; a full health bar of damage taken
// costs one, charged pro rata so free clusters win over equal-value monsters.
constexpr float recall_turns = 11;
// Most bodies on one group: past three, a fourth adds little but splits the reward further.
constexpr int max_group_bodies = 2;
// Value kept by a body joining a group that does not need it (see choose_target).
constexpr float extra_body_value = 0.4538f;
// Extra turns charged to starting a group that needs help, for the wait until it arrives.
constexpr float help_wait_turns = 50;
// The duo carries the most gems and Cassi has 700 health: it leaves earlier when enemies are around.
constexpr float recall_health = 0.2880f, duo_recall_health = 0.7487f;
constexpr float spawn_leave_health = 0.7716f;
constexpr int tribute_gems = 25;      // always bank at least this many
constexpr int tribute_detour = 10;     // bank anything when an altar is this close
constexpr int engage_radius = 4;
// Extra path tiles we accept to reach a free altar instead of clearing a lock (~3 attack turns).
constexpr int lock_detour = 6;
constexpr int lock_place_range = 2;   // altar locks are placed within two tiles
// Sieges (v6): monolith groups are farmable. They never chase or heal, so damage carries over
// between trips: hit until hurt, step out of reach, recall, come back. A siege plans on this many
// bodies (capped by the team), is never ruled out by risk (its damage is charged as trips home),
// and is valued at `siege_value` times its reward. siege = 0 restores v5 (monoliths skipped).
constexpr int siege = 1;
// Fights (v6). ally_radius: allies this close count toward our side of the health comparison
// (engage_radius only counted allies within 4, so a friend 6 tiles away didn't help). retreat_to_allies:
// when outmatched, fall back toward the nearest visible ally instead of spawn. threat_tribute_gems:
// for threat_memory rounds after seeing an enemy, bank at this many gems (carried gems feed killers).
constexpr int ally_radius = 4;
constexpr int retreat_to_allies = 0;
constexpr int threat_tribute_gems = 25;
constexpr int threat_memory = 30;
// v7: react to damage from attackers we cannot see (see take_turn). 0 = v6 behaviour.
constexpr int hidden_response = 1;
// v7: a body carrying gems leaves at this health fraction while an enemy was seen in the last
// threat_memory rounds (ranked: 45 of 79 deaths with gems came after the carrier was below 50%).
constexpr float carrier_recall_health = 0.5f;
constexpr int jungle_first = 0;
constexpr int max_siege_bodies = 6;
constexpr float siege_value = 1.0f;
// v7: turns of a sieged group's damage (shared by the allies with us) we keep in hand to walk out of range.
constexpr float siege_escape_turns = 3.0f;
// v8: turns of a Monolith's hits a siege body must be able to take on top of walking out of its range.
constexpr float monolith_hit_turns = 3.0f;

int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }

cambc::bitboard tiles_around(vec2 position, int radius) {
    cambc::bitboard tiles;
    tiles.clear();
    tiles.draw_chebyshev(position, radius);
    return tiles;
}

cambc::bitboard terrain_blocked() {
    return cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(team::enemy);
}

// Terrain plus visible occupants, excluding our own tile.
cambc::bitboard blocked_tiles() {
    auto blocked = terrain_blocked() | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
        | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

float ability_range(const cambc::ability& ability) {
    const auto& info = ability.info();
    if (!info)
        return 0;
    return std::visit([](const auto& payload) -> float {
        if constexpr (requires { payload.range; }) return payload.range;
        else return 0.f;
    }, *info);
}

bool excludes_walking(const cambc::ability& ability) {
    const auto& info = ability.info();
    return info && std::visit([](const auto& payload) { return payload.exclusive_with_movement; }, *info);
}

// Shard stat per champion_type: casters buy focus, Ryth health (it also adds strength), the rest strength.
enum class stat { strength, health, focus };
constexpr float power_health_share = 0.4f;  // v9: fraction of Power (v11: Bruiser) shards bought as health
constexpr std::array<stat, 13> build_stat{
    stat::health,   stat::strength, stat::strength, stat::focus, stat::strength, stat::focus,
    stat::focus,    stat::focus,    stat::focus,    stat::strength, stat::strength, stat::focus,
    stat::focus,  // heart_of_ruin (week 2 patch)
};
// v12 (B25): Marcella (focus growth 10 vs strength 5; her spell is 2x focus) and Heart of Ruin (dash damage scales
// with focus) buy focus, as the top 20 do (Marcella Bruiser:Focus 32 a game, Heart of Ruin Bruiser:Focus 14.9).

// Build order (the +200 elo bot's): one Monster emblem of our stat (10% more camp damage), one
// Durability emblem of health (10% less damage taken), then Bruiser shards of our stat forever (v11 B21:
// 5 of the stat plus 1 of each other, 7 shards' worth vs Power's 6; the top 20 buy mostly Bruiser).
// Emblem passives do not stack, so a second emblem of a family only adds stats.
std::array<cambc::item_type, 3> build_order(stat s) {
    using cambc::item_type;
    // v7 `jungle_first`: the first emblem is Jungle (30% less damage from camps) instead of Monster
    // (10% more damage to camps). Same five shards and stats; only the passive changes. Ranked: we take
    // as much damage from camps as from champions, and 54% of opponent champions finish a Jungle emblem.
    switch (s) {
    case stat::strength:
        return {jungle_first ? item_type::jungle_shard_of_strength : item_type::monster_shard_of_strength,
                item_type::durability_shard_of_health, item_type::bruiser_shard_of_strength};
    case stat::health:
        return {jungle_first ? item_type::jungle_shard_of_health : item_type::monster_shard_of_health,
                item_type::durability_shard_of_health, item_type::bruiser_shard_of_health};
    default:
        return {jungle_first ? item_type::jungle_shard_of_focus : item_type::monster_shard_of_focus,
                item_type::durability_shard_of_health, item_type::bruiser_shard_of_focus};
    }
}

// A champion we can see right now, with the fields we use read once. Ally occupancy can include
// allies outside our own vision, so look each tile up defensively and skip what we cannot see.
struct seen_champion {
    cambc::champion unit;
    vec2 position;
    float health, max_health;
    float damage;  // v10: the larger of strength and focus, a proxy for damage per turn
};

std::vector<seen_champion> visible_champions(team side) {
    std::vector<seen_champion> result;
    for (const auto p : (cambc::vision::champion_tiles(side) & cambc::vision::visible_tiles()).positions()) {
        try {
            auto unit = cambc::vision::champion_at(p);
            if (unit.is_alive())
                result.push_back({unit, p, unit.health(), unit.max_health(), std::max(unit.strength(), unit.focus())});
        } catch (const cambc::error&) {
        }
    }
    return result;
}

// Our damage per turn against camps: a basic attack (strength) every turn plus each camp-targeting
// spell's damage spread over its cooldown. Casters like Cassi sit well below the per-body figure
// (~45 against clusters), so the real number keeps them off groups they cannot clear in time.
// Engine calls cost fuel, so this is measured once per turn.
float measure_damage() {
    auto& me = cambc::self();
    float damage = me.strength();
    for (const auto& ability : me.abilities()) {
        const auto& info = ability.info();
        if (!info || !(ability.is_damage() || ability.is_aoe_damage()) || !ability.can_target_camps())
            continue;
        try {
            const int cooldown = std::visit([](const auto& payload) { return payload.max_cooldown; }, *info);
            damage += ability.damage_amount() / static_cast<float>(std::max(1, cooldown));
        } catch (const cambc::error&) {
        }
    }
    return damage;
}

// Can we plus `helpers` average allies kill the group before its damage costs too much health?
bool survivable(const group_info& group, int helpers, const self_estimate& own) {
    const float damage = own.damage + body_damage * helpers;
    const float health = own.health + body_health * helpers;
    return group.exposure / damage <= max_damage_share * health;
}

// Monsters do not chase; since the week 2 patch elks and golems hit at range 4 and monoliths at 5.
// Is anything that hurts within reach of `p`?
bool threatened(const world& camps, vec2 p) {
    for (const auto tile : (cambc::vision::camp_tiles() & tiles_around(p, 5)).positions())
        if (const auto* camp = camps.camp_at(tile); camp && !camp->is_gem_cluster() && !camp->is_gold_cluster()
            && distance(p, tile) <= camp_range(camp->kind()))
            return true;
    return !(cambc::vision::champion_tiles(team::enemy) & tiles_around(p, 4)).is_empty();
}

// Shared memory as one word: a single engine call each way (~150 fuel per call).
std::uint64_t read_memory() {
    std::array<std::byte, 8> bytes{};
    cambc::shared_memory.read(0, bytes);
    std::uint64_t word = 0;
    for (int i = 0; i < 8; ++i)
        word |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    return word;
}

void write_memory(std::uint64_t word) {
    std::array<std::byte, 8> bytes{};
    for (int i = 0; i < 8; ++i)
        bytes[i] = static_cast<std::byte>(word >> (8 * i));
    cambc::shared_memory.write(0, std::span<const std::byte>(bytes));
}

int get_bits(std::uint64_t word, int offset, int count) {
    return static_cast<int>((word >> offset) & ((std::uint64_t{1} << count) - 1));
}

std::uint64_t set_bits(std::uint64_t word, int offset, int count, int value) {
    const auto mask = ((std::uint64_t{1} << count) - 1) << offset;
    return (word & ~mask) | ((static_cast<std::uint64_t>(value) << offset) & mask);
}

// Group index claimed by body `slot`, or -1.
int claim_of(std::uint64_t word, int slot) { return get_bits(word, slot * claim_bits, claim_bits) - 1; }

void mark_danger(vec2 p, int round) {
    auto word = read_memory();
    word = set_bits(word, danger_x_bit, 7, p.x + 1);
    word = set_bits(word, danger_y_bit, 7, p.y);
    word = set_bits(word, danger_round_bit, 8, round & 0xFF);
    write_memory(word);
}

// Was a group of enemies seen near `p` recently? Rounds are stored mod 256, so compare that way.
bool near_danger(std::uint64_t word, vec2 p, int round) {
    const int x = get_bits(word, danger_x_bit, 7);
    if (x == 0)
        return false;
    const int age = (round - get_bits(word, danger_round_bit, 8)) & 0xFF;
    return age <= danger_rounds && distance(p, vec2{x - 1, get_bits(word, danger_y_bit, 7)}) <= danger_radius;
}

} // namespace

void Bot::init() {
    initialised = true;
    plan = read_plan();
    const auto type = cambc::self().champion_name();
    const int mine = static_cast<int>(type);
    if (plan) {
        squad = plan->role_of(type) == role::gem_duo ? role::gem_duo : role::gold_squad;
        // Our claim slot: rank of our champion_type among the team's (Cassi and Issac are two).
        slot = team_bodies = 0;
        for (int t = 0; t < static_cast<int>(std::size(plan->roles)); ++t)
            if (plan->roles[t] != role::none) {
                ++team_bodies;
                slot += t < mine;
            }
    } else {
        slot = mine % max_bodies;
    }
    slot = std::min(slot, max_bodies - 1);
    camps.init();

    // Static trip lengths per group for the target rate. Unreachable counts as far, not free.
    const auto blocked = terrain_blocked();
    cambc::bitboard altar_tiles;
    altar_tiles.clear();
    for (const auto altar : cambc::map::altars().positions())
        altar_tiles |= tiles_around(altar, cambc::constants::submit_gems_range);
    // v8: camp spawn tiles are only free while the camp is dead (01boss-battle's one altar is walled in
    // by two Monoliths). With no other tile to stand on, gems are worthless: farm gold for the tiebreak.
    for (const auto& g : camps.groups())
        altar_tiles &= ~g.tiles;
    gems_deliverable = !(altar_tiles & ~blocked).is_empty();
    // v13 (B29): 02the-pack's only altar sits inside a 48-wolf ring. Carriers stood at the ring all game
    // (ranked and local games 0:0), so note when every walk from our spawn to an altar crosses a camp.
    if (gems_deliverable) {
        cambc::bitboard all_camps, visited;
        all_camps.clear();
        for (const auto& g : camps.groups())
            all_camps |= g.tiles;
        cambc::distance_field ring;
        ring.compute((blocked | all_camps) & ~altar_tiles, altar_tiles & ~blocked, visited);
        altar_walled = true;
        for (const auto tile : cambc::map::spawn_region(team::ally).positions())
            altar_walled &= ring.get(tile) == cambc::distance_field::unreachable;
        if (altar_walled) {
            // 0-1 BFS from our spawn, a kill per camp tile entered. Every champion cuts the camp tiles of
            // the cheapest path, so they gather at one place (on 02the-pack, one wolf via a diagonal).
            // Bitboard reads are engine calls (~150 fuel): copy the masks into local grids first.
            const int w = cambc::map::width(), h = cambc::map::height();
            std::vector<char> cell(w * h, 0);  // 1 blocked, 2 camp, 4 altar tile
            for (const auto p : blocked.positions()) cell[p.y * w + p.x] |= 1;
            for (const auto p : all_camps.positions()) cell[p.y * w + p.x] |= 2;
            for (const auto p : altar_tiles.positions()) cell[p.y * w + p.x] |= 4;
            std::vector<int> cost(w * h, 1 << 20), prev(w * h, -1);
            std::deque<int> queue;
            for (const auto tile : cambc::map::spawn_region(team::ally).positions()) {
                cost[tile.y * w + tile.x] = 0;
                queue.push_back(tile.y * w + tile.x);
            }
            int end = -1;
            while (!queue.empty() && end < 0) {
                const int i = queue.front();
                queue.pop_front();
                if (cell[i] & 4) {
                    end = i;
                    break;
                }
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        const int x = i % w + dx, y = i / w + dy, j = y * w + x;
                        if (x < 0 || y < 0 || x >= w || y >= h || (cell[j] & 1))
                            continue;
                        const int kill = (cell[j] & 2) != 0;
                        if (cost[i] + kill < cost[j]) {
                            cost[j] = cost[i] + kill;
                            prev[j] = i;
                            kill ? queue.push_back(j) : queue.push_front(j);
                        }
                    }
            }
            corridor.clear();
            corridor_beside.clear();
            corridor_gather.clear();
            for (int i = end; i >= 0; i = prev[i])
                if (cell[i] & 2) {
                    corridor.set(vec2{i % w, i / w});
                    corridor_beside |= tiles_around(vec2{i % w, i / w}, 1);
                    corridor_gather |= tiles_around(vec2{i % w, i / w}, 3);
                }
            corridor_beside &= ~all_camps & ~blocked;
            corridor_gather &= ~all_camps & ~blocked;
            std::cout << "plan: altar walled in, corridor of " << corridor.count() << " camp tiles\n";
        }
    }
    auto distances_from = [&](cambc::bitboard sources, int group_info::*field, cambc::distance_field& distances) {
        cambc::bitboard visited;
        distances.compute(blocked & ~sources, sources & ~blocked, visited);
        for (auto& g : camps.groups()) {
            const int d = distances.get(g.anchor);
            g.*field = d == cambc::distance_field::unreachable ? 100 : d;
        }
    };
    cambc::distance_field home_walk;
    distances_from(cambc::map::spawn_region(team::ally), &group_info::home_distance, home_walk);
    distances_from(altar_tiles, &group_info::altar_distance, altar_walk);
    std::cout << "plan: " << cambc::to_string(type) << (squad == role::gem_duo ? " gem_duo" : " gold_squad")
              << " slot=" << slot << " bodies=" << team_bodies << (plan ? "" : " (no plan)") << '\n';
}

void Bot::shop() {
    auto& me = cambc::self();
    // Power shards are not kept as items once 6 combine into an emblem: count both.
    auto owned_count = [&](cambc::item_type shard) {
        const auto items = me.item_count();
        const auto emblem = shard.result();
        return static_cast<unsigned>(items[shard] + (emblem ? items[*emblem] * shard.combine_count() : 0));
    };
    // An altar lock first: 10 gold to cancel an enemy tribute (only sold on spawn or while dead).
    const auto lock = cambc::consumable_type::altar_lock;
    if (me.consumables()[lock].count() == 0 && me.can_buy(lock))
        me.buy(lock);

    // Finish each emblem before starting the next; Bruiser (last) soaks up the rest.
    const auto order = build_order(build_stat[static_cast<int>(me.champion_name())]);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto shard = order[i];
        const auto owned = me.item_count();
        const bool last = i + 1 == order.size();
        const auto emblem = shard.result();
        if (!last && emblem && owned[*emblem] > 0)
            continue;
        if (last && power_health_share > 0 && shard != cambc::item_type::bruiser_shard_of_health) {
            // v9: split the last shards (v11: Bruiser) between our stat and health (Jame v3 and ranked opponents do).
            const auto health = cambc::item_type::bruiser_shard_of_health;
            while (me.gold_count() >= shard.cost()) {
                const float share = float(owned_count(health)) / std::max(1u, owned_count(health) + owned_count(shard));
                const auto pick = share < power_health_share ? health : shard;
                if (!me.can_buy(pick, 1)) return;
                me.buy(pick, 1);
            }
            return;
        }
        unsigned count = me.gold_count() / shard.cost();
        if (!last)
            count = std::min<unsigned>(count, shard.combine_count() - owned[shard]);
        if (count == 0 || !me.can_buy(shard, count))
            return;
        me.buy(shard, count);
    }
}

// Drop our altar lock on an altar beside us where an enemy stands: placing it cancels their tribute.
bool Bot::place_lock() {
    auto& me = cambc::self();
    const auto& lock = me.consumables()[cambc::consumable_type::altar_lock];
    if (lock.count() == 0)
        return false;
    const auto pos = me.position();
    const auto enemies = cambc::vision::champion_tiles(team::enemy);
    for (const auto altar : cambc::map::altars().positions()) {
        if (distance(pos, altar) > lock_place_range || cambc::vision::has_altar_lock(altar)
            || (enemies & tiles_around(altar, cambc::constants::submit_gems_range)).is_empty())
            continue;
        try {
            if (lock.use(altar))
                return true;
        } catch (const cambc::error& error) {
            std::cout << "lock failed: " << error.what() << '\n';
        }
    }
    return false;
}

bool Bot::move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    const auto origin = me.position();
    if (!me.can_move() || goals.is_set(origin))
        return false;

    // Only free goals or occupied camp tiles can seed the field.
    // v9: also block living camps out of sight. With only visible camps blocked, a path through a camp
    // opened whenever we stepped out of view of it and closed when we saw it again (01the-farm: four
    // carriers oscillated beside the wolves at 24,19 for 1500 rounds, ranked 1-9).
    auto blocked = blocked_tiles();
    const auto unseen = ~cambc::vision::visible_tiles();
    const int round = static_cast<int>(cambc::round_number());
    for (const auto& g : camps.groups())
        if (!g.reward_ready && !g.known_respawning(round))
            blocked |= g.tiles & unseen;
    goals &= ~blocked | cambc::vision::camp_tiles();
    if (goals.is_empty())
        return false;

    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(origin, 0), visited);

    // A step into a hidden unit (bush) returns false without using the move: try the next-best tile
    // (v7; ranked: a Vendetta stood still for 4 turns while a hidden Tavi killed it).
    auto options = blocked.reachable_tiles(origin, me.base_stats().base_move_speed);
    auto step = origin;
    for (int attempt = 0; attempt < 3; ++attempt) {
        step = origin;
        for (auto tile : options.positions())
            if (field.get(tile) < field.get(step)) step = tile;
        if (step == origin)
            return false;
        try {
            if (me.move_absolute(step))
                break;
        } catch (const cambc::error&) {
            // tile became known-occupied since our snapshot
        }
        options.set(step, false);
        step = origin;
    }
    if (step == origin)
        return false;
    moved = true;

    // Dash further along the same field when one is ready (dashes allow walking).
    if (field.get(step) > 2 && me.can_cast())
        for (const auto& ability : me.abilities())
            if (ability.is_dash() && ability.is_ready()) {
                auto landing = step;
                for (auto tile : (ability.valid_tiles() & ~blocked_tiles()).positions())
                    if (field.get(tile) < field.get(landing)) landing = tile;
                try {
                    if (landing != step) ability.use(landing);
                } catch (const cambc::error&) {
                    // Illegal dash path (walls in between): keep the walk.
                }
                break;
            }
    return true;
}

// Position of a target that is still alive and in vision. Targets die or leave vision mid-turn
// (our own hit kills them, our move loses sight), and querying them then throws.
template <class Target>
std::optional<vec2> live_position(const Target& target) {
    try {
        if (target.is_visible() && target.health() > 0)
            return target.position();
    } catch (const cambc::error&) {
    }
    return std::nullopt;
}

// Basic attack if in range, then offensive casts. Returns whether we attacked or cast.
template <class Target>
bool Bot::strike(const Target& target) {
    auto& me = cambc::self();
    const auto position = live_position(target);
    if (!position)
        return false;
    bool acted = false;
    if (me.can_attack() && distance(me.position(), *position) <= me.attack_range()) {
        me.attack(target);
        acted = true;
    }
    const bool could_cast = me.can_cast();
    cast_offense(target);
    return acted || (could_cast && !me.can_cast());
}

template <class Target>
void Bot::cast_offense(const Target& target) {
    auto& me = cambc::self();
    constexpr bool at_champion = std::is_same_v<Target, cambc::champion>;
    for (const auto& ability : me.abilities()) {
        if (!me.can_cast())
            return;
        const auto position = live_position(target);
        if (!position)
            return;
        if (!ability.info() || !ability.is_ready() || (moved && excludes_walking(ability)))
            continue;
        const int d = distance(me.position(), *position);
        try {
            if (ability.is_attack_reset()) {
                // Restores the attack allowance: also lets ranged champions walk and still attack.
                if (!me.can_attack() && d <= me.attack_range() && ability.use() && me.can_attack())
                    me.attack(target);
            } else if (ability.is_damage() || ability.is_cc()) {
                const bool allowed = at_champion ? ability.can_target_enemies() : ability.can_target_camps();
                if (allowed && d <= ability_range(ability))
                    ability.use(target);
            } else if (ability.is_aoe_damage()) {
                if (d <= ability_range(ability))
                    ability.use(*position);
            }
        } catch (const cambc::error& error) {
            std::cout << "cast failed: " << error.what() << '\n';
        }
    }
}

// Heal the most hurt visible ally; shield ourselves when hurt and under threat.
void Bot::cast_support() {
    auto& me = cambc::self();
    if (!me.can_cast())
        return;
    for (const auto& ability : me.abilities()) {
        if (!ability.info() || !ability.is_ready())
            continue;
        try {
            if (ability.is_heal()) {
                const seen_champion* best = nullptr;
                const auto allies = visible_champions(team::ally);
                float worst = 0.7f;
                for (const auto& ally : allies) {
                    if (ally.unit == me || distance(me.position(), ally.position) > ability_range(ability))
                        continue;
                    if (const float ratio = ally.health / ally.max_health; ratio < worst) {
                        worst = ratio;
                        best = &ally;
                    }
                }
                if (best) { ability.use(best->unit); return; }
            } else if (ability.is_shield() && me.health() < 0.85f * me.max_health()) {
                ability.use();
                return;
            }
        } catch (const cambc::error& error) {
            std::cout << "support cast failed: " << error.what() << '\n';
        }
    }
}

void Bot::retreat() {
    move_towards_closest(cambc::map::spawn_region(team::ally));
}

// Weigh the fight by total visible health within reach, ours against theirs: head counts froze a
// v3-vs-v2 game in a 4-vs-5 standoff we would have won on health. Fight at parity or better (or to
// finish a nearly dead enemy); otherwise mark the spot as dangerous, fall back, and take whatever
// shot is still legal. The duo chases; the gold squad only hits what is in range.
bool Bot::fight_enemies(int round) {
    auto& me = cambc::self();
    const auto pos = me.position();
    const seen_champion* weakest = nullptr;
    float enemy_health = 0, ally_health = 0, enemy_damage = 0, ally_damage = 0;
    vec2 centre{0, 0};
    int enemies_near = 0;
    const auto enemies = visible_champions(team::enemy);
    for (const auto& enemy : enemies) {
        if (distance(pos, enemy.position) > engage_radius)
            continue;
        ++enemies_near;
        enemy_health += enemy.health;
        enemy_damage += enemy.damage;
        centre = centre + enemy.position;
        if (!weakest || enemy.health < weakest->health)
            weakest = &enemy;
    }
    if (!weakest)
        return false;
    const seen_champion* friend_near = nullptr;
    const auto allies = visible_champions(team::ally);
    for (const auto& ally : allies) {
        if (distance(pos, ally.position) <= std::max(engage_radius, ally_radius)) {
            ally_health += ally.health;
            ally_damage += ally.damage;
        }
        if (!(ally.unit == me) && distance(pos, ally.position) >= 2
            && (!friend_near || distance(pos, ally.position) < distance(pos, friend_near->position)))
            friend_near = &ally;
    }

    const auto& target = weakest->unit;
    // v10: also fight when behind on health but ahead on fighting power (health times damage, Lanchester-style).
    // Power alone: 52% vs v9; requiring both: 43%; either: 57% (and 59% vs Jame v3, v9: 55%).
    const float ours = ally_health * ally_damage, theirs = enemy_health * enemy_damage;
    const bool even = ours >= theirs || ally_health >= enemy_health;
    const bool finish = weakest->health < 0.3f * weakest->max_health && ours >= 0.7f * theirs;
    if (!even && !finish) {
        mark_danger(vec2{centre.x / enemies_near, centre.y / enemies_near}, round);
        if (retreat_to_allies && friend_near && distance(pos, friend_near->position) <= 12)
            move_towards_closest(tiles_around(friend_near->position, 1));
        else
            retreat();
        strike(target);  // melee can still hit after walking; casts that exclude walking are skipped
        return true;
    }
    const bool hit = strike(target);
    if (hit || squad == role::gold_squad)
        return hit;
    // Duo: close in; melee can still attack after walking.
    if (move_towards_closest(tiles_around(weakest->position, 1)))
        strike(target);
    return true;
}

bool Bot::should_tribute(int round) {
    auto& me = cambc::self();
    const int gems = static_cast<int>(me.gem_count());
    if (gems == 0 || !gems_deliverable)
        return false;
    // Once heading to an altar, keep going: on interlock a Chebyshev check flipped every turn
    // between "altar close" and "back to the camp" where walls make the walk long.
    const bool threat = last_enemy_round >= 0 && round - last_enemy_round <= threat_memory;
    if (banking || gems >= (threat ? std::min(tribute_gems, threat_tribute_gems) : tribute_gems))
        return banking = true;
    // Walking distance: a nearby altar behind a wall is not a short detour.
    if (static_cast<int>(altar_walk.get(me.position())) <= tribute_detour)
        return banking = true;
    // Nothing worth capturing right now: bank what we hold.
    return banking = choose_target(round) < 0;
}

// Tribute at the nearest free altar, or break an enemy lock (three basic attacks; ranged champions
// hit it from range) when a free altar is more than `lock_detour` tiles further. Skipping locked
// altars outright stalled v2 for 300 rounds once every altar was locked.
void Bot::go_tribute() {
    auto& me = cambc::self();
    const auto pos = me.position();
    const int submit_range = cambc::constants::submit_gems_range;
    const int hit_range = me.attack_range();
    const auto enemy_locks = cambc::vision::altar_lock_tiles(team::enemy);

    const auto altars = cambc::map::altars().positions();
    for (const auto altar : altars)
        if (!enemy_locks.is_set(altar) && distance(pos, altar) <= submit_range) {
            me.submit_gems(altar);
            return;
        }
    cambc::bitboard free_goals, locked_goals;
    free_goals.clear();
    locked_goals.clear();
    for (const auto altar : altars) {
        const bool locked = enemy_locks.is_set(altar);
        if (locked && distance(pos, altar) <= hit_range && me.can_attack()) {
            try {
                me.attack(cambc::vision::altar_lock_at(altar));
            } catch (const cambc::error& error) {
                std::cout << "lock attack failed: " << error.what() << '\n';
            }
            return;
        }
        (locked ? locked_goals : free_goals) |= tiles_around(altar, locked ? hit_range : submit_range);
    }

    // Both kinds available: compare walking distances to each set.
    auto walk = [&](const cambc::bitboard& goals) {
        if (goals.is_empty())
            return static_cast<int>(cambc::distance_field::unreachable);
        const auto blocked = terrain_blocked();
        cambc::distance_field field;
        cambc::bitboard visited;
        field.compute_to(blocked, goals & ~blocked, tiles_around(pos, 0), visited);
        return static_cast<int>(field.get(pos));
    };
    const bool use_free = locked_goals.is_empty()
        || (!free_goals.is_empty() && walk(free_goals) <= walk(locked_goals) + lock_detour);
    const auto& goals = use_free ? free_goals : locked_goals;
    if (!move_towards_closest(goals) && altar_walled && !goals.is_set(pos))
        breach();
}

// v13 (B29): cut the corridor (see init) through the camp walling the altar in: gather beside it, then hit
// its members once the team is here. Damage wakes the whole camp and every member in range hits the nearest
// champion, so a lone carrier waits (one died at the ring three times a game).
void Bot::breach() {
    auto& me = cambc::self();
    const auto pos = me.position();
    const auto live = corridor & cambc::vision::camp_tiles();
    const cambc::camp* best = nullptr;
    for (const auto p : live.positions())
        if (distance(pos, p) <= me.attack_range() && (!best || distance(pos, p) < distance(pos, best->spawn_position())))
            best = camps.camp_at(p);
    int allies_near = 0;
    for (const auto& ally : visible_champions(team::ally))
        allies_near += !(ally.unit == me) && corridor_gather.is_set(ally.position);  // v14 (B29b): in the zone, not near me
    const bool gathered = allies_near >= std::min(2, team_bodies - 1);
    if (best) {
        if (gathered)
            strike(*best);
        return;
    }
    // Two tiles may touch the corridor: wait within three of it, step up once the team is here.
    if (!(gathered && move_towards_closest(corridor_beside)))
        move_towards_closest(corridor_gather);
}

// Gold only helps once spent: head home to shop when carrying an emblem's worth (Tavi shops anywhere).
bool Bot::shop_trip() {
    auto& me = cambc::self();
    const unsigned threshold = squad == role::gold_squad ? 150 : 300;
    if (me.champion_name() == cambc::champion_type::tavi || me.gold_count() < threshold || me.gem_count() > 0)
        return false;
    if (auto* target = camps.group(current_target))
        if (target->reward_ready && target->tiles.is_set(me.position()))
            return false;  // mid-capture
    me.recall();
    return true;
}

// Best gems-and-gold per turn for this body. A group is started with the fewest bodies that survive
// it, and stays open to joiners up to `max_group_bodies`; every body splits its value. Computed
// once per turn.
int Bot::monolith_exit_turns() const {
    return camp_range(cambc::camp_type::monolith) + 1 - std::min(cambc::self().attack_range(), camp_range(cambc::camp_type::monolith));
}

int Bot::choose_target(int round) {
    if (target_round == round)
        return current_target;
    target_round = round;
    const auto word = read_memory();
    auto& groups = camps.groups();
    const int count = std::min<int>(static_cast<int>(groups.size()), (1 << claim_bits) - 1);
    const int max_helpers = std::max(0, std::min(team_bodies, max_group_bodies) - 1);

    // Bodies needed for group i (0 = too much even with help) and allies already claiming it.
    auto needed = [&](const group_info& g) {
        for (int helpers = 0; helpers <= max_helpers; ++helpers)
            if (survivable(g, helpers, own)) return helpers + 1;
        return 0;
    };
    auto claimants = [&](int index) {
        int n = 0;
        for (int s = 0; s < max_bodies; ++s)
            n += s != slot && claim_of(word, s) == index;
        return n;
    };

    const int siege_bodies = std::max(1, std::min(team_bodies, max_siege_bodies));
    // v7: siege any group too big for max_group_bodies, not only monoliths (stampede's 6-elk group holds
    // all its gems; eight/pilgrimage/swirl 22-49%). Dynamic: once we are strong enough, it is a normal target.
    auto sieged = [&](const group_info& g) { return g.monolith || (g.mobs && needed(g) == 0); };
    auto cap = [&](const group_info& g) { return sieged(g) ? siege_bodies : max_group_bodies; };

    // Keep the current target while it is up soon and not taken over.
    if (auto* current = camps.group(current_target); current && current_target < count)
        if ((sieged(*current) ? siege != 0 : needed(*current) > 0) && claimants(current_target) < cap(*current)
            && current->respawn_round - round <= 10 && current->skip_until <= round
            && !near_danger(word, current->anchor, round))
            return current_target;

    const auto origin = cambc::self().position();
    if (distance(origin, target_field_origin) >= 3 || round - target_field_round >= 10) {
        cambc::bitboard visited;
        target_field.compute(terrain_blocked(), tiles_around(origin, 0), visited);
        target_field_origin = origin;
        target_field_round = round;
    }
    const auto& field = target_field;

    int best = -1;
    float best_rate = 0;
    for (int i = 0; i < count; ++i) {
        const auto& g = groups[i];
        const bool is_siege = sieged(g);
        groups[i].siege_target = is_siege;  // for work_group and the leave check
        if ((is_siege && !siege) || g.skip_until > round || near_danger(word, g.anchor, round))
            continue;
        if (is_siege) {
            // Siege: plan on the whole team (capped), kill what is left, pay its damage in trips home.
            const int travel = field.get(g.anchor);
            if (travel == cambc::distance_field::unreachable)
                continue;
            const int others = claimants(i);
            const int bodies = std::max(siege_bodies, others + 1);
            const float damage = own.damage + body_damage * (bodies - 1);
            const float kill = g.remaining / damage;
            // Damage taken shrinks as members die: the group's exposure scaled to what is left.
            const float taken = g.exposure * (g.remaining / std::max(1.f, g.health)) / damage / bodies;
            const float gems = static_cast<float>(g.gems) / bodies;
            float time = std::max<float>(travel, g.respawn_round - round) + kill + capture_turns
                + g.altar_distance + std::ceil(gems / 10)
                + taken / own.health * (2 * g.home_distance + recall_turns);
            if (others + 1 < bodies)
                time += help_wait_turns;
            // v8: a Monolith hits the nearest champion for its full damage out to range 5. Skip it unless we
            // can take `monolith_hit_turns` turns of hits and still get out: one step when allies siege with
            // us (it switches to whoever is nearer), else the walk out of range from where we attack.
            const int exit = siege_bodies > 1 ? 1 : monolith_exit_turns();
            if (g.monolith && own.health < (exit + monolith_hit_turns) * g.damage)
                continue;
            const float value = siege_value * (gem_value(g) + gold_weight * g.gold) / bodies;
            if (value / time > best_rate) {
                best_rate = value / time;
                best = i;
            }
            continue;
        }
        // Join a claimed group as one more body (its value split further), or start one with the
        // fewest bodies that survive it. Joining wins when groups are scarce (rows: three groups
        // per side for five bodies) and halves the damage a lone melee body would tank.
        const int need = needed(g);
        const int others = claimants(i);
        const int bodies = std::max(need, others + 1);
        if (need == 0 || bodies > max_group_bodies)
            continue;
        const int travel = field.get(g.anchor);
        if (travel == cambc::distance_field::unreachable)
            continue;
        const float damage = own.damage + body_damage * (bodies - 1);
        const float taken = g.exposure / damage / bodies;  // our share of the damage
        // One cycle: get there (or wait out the respawn), kill, capture, carry our share of the gems
        // to an altar and channel them in; damage taken costs that share of a trip home and back.
        const float gems = gem_value(g) / bodies;
        float time = std::max<float>(travel, g.respawn_round - round) + g.health / damage + capture_turns
            + (gems > 0 ? g.altar_distance + std::ceil(gems / 10) : 0)
            + taken / own.health * (2 * g.home_distance + recall_turns);
        if (bodies > 1 && others + 1 < bodies)
            time += help_wait_turns;
        // A body past what the group needs only speeds it up: its share comes out of the
        // claimants' shares, so value it at a discount.
        float value = (gem_value(g) + gold_weight * g.gold) / bodies;
        if (bodies > need)
            value *= extra_body_value;
        if (value > 0 && value / time > best_rate) {
            best_rate = value / time;
            best = i;
        }
    }
    current_target = best;
    return best;
}

// Publish this turn's claim (or none) when it changed.
void Bot::publish_claim(int round) {
    if (published_claim == claim && published_round + 50 > round)
        return;  // refresh now and then in case a write was lost to running out of fuel
    const auto word = read_memory();
    write_memory(set_bits(word, slot * claim_bits, claim_bits, claim + 1));
    published_claim = claim;
    published_round = round;
}

void Bot::work_group(int index, int round) {
    auto& group = camps.groups()[index];
    auto& me = cambc::self();
    const auto pos = me.position();

    // Fight the group's living members: monsters before clusters, nearest first.
    std::optional<cambc::camp> target;
    int target_key = 1 << 30;
    const auto live = group.tiles & cambc::vision::camp_tiles() & cambc::vision::visible_tiles();
    for (const auto p : live.positions()) {
        const auto* camp = camps.camp_at(p);
        if (!camp)
            continue;
        const bool cluster = camp->is_gem_cluster() || camp->is_gold_cluster();
        const int key = (cluster ? 1000 : 0) + distance(pos, p);
        if (key < target_key) { target_key = key; target = *camp; }
    }
    if (target && !target->is_gem_cluster() && !target->is_gold_cluster() && !(group.siege_target && siege)) {
        int helpers = 0;
        for (const auto& ally : visible_champions(team::ally))
            helpers += !(ally.unit == me) && distance(ally.position, group.anchor) <= 6;
        if (!survivable(group, helpers, own)) {
            // Wait for help at a safe distance (camps hit at range 3 and do not chase); give up if
            // nobody comes.
            waited = waiting_for == index ? waited + 1 : 1;
            waiting_for = index;
            if (waited > max_wait_turns) {
                group.skip_until = round + give_up_rounds;
                current_target = claim = -1;
            }
            if (threatened(camps, pos)) retreat();
            else if (distance(pos, group.anchor) > 5) move_towards_closest(group.tiles);
            return;
        }
    }
    if (target) {
        if (strike(*target)) {
            hit_target = true;
            return;
        }
        cambc::bitboard goals;
        goals.clear();
        goals.set(target->spawn_position());
        if (move_towards_closest(goals))
            hit_target = strike(*target);
        return;
    }

    // Everything dead and the reward waiting: stand on a spawn tile until the capture completes.
    if (group.reward_ready) {
        if (!group.tiles.is_set(pos))
            move_towards_closest(group.tiles);
        return;
    }

    // Respawning: an occupied spawn tile delays the whole group, so wait beside the tiles, never on
    // them. Otherwise (unseen, or alive members out of sight) walk up beside them.
    cambc::bitboard beside;
    beside.clear();
    for (const auto tile : group.tiles.positions())
        beside |= tiles_around(tile, 1);
    beside &= ~group.tiles;
    const bool waiting = group.known_respawning(round) && group.seen_round == round;
    if (group.tiles.is_set(pos) || !waiting)
        move_towards_closest(beside);
}

void Bot::play_turn() {
    if (!initialised)
        init();
    const int round = static_cast<int>(cambc::round_number());
    camps.observe(round);
    moved = false;
    if (cambc::self().is_alive())
        own = {measure_damage(), cambc::self().max_health()};
    claim = -1;  // held only while working the target (or fighting near it)
    take_turn(round);
    publish_claim(round);
}

void Bot::take_turn(int round) {
    auto& me = cambc::self();
    if (!me.is_alive()) {
        shop();
        return;
    }
    // Leave channels running.
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;

    if (me.gem_count() == 0)
        banking = false;
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (me.gold_count() >= 10 && (at_spawn || me.champion_name() == cambc::champion_type::tavi))
        shop();
    // Stunned: any action would throw "champion cannot act".
    if (!me.can_move() && !me.can_attack() && !me.can_cast())
        return;

    // Unseen attackers (v7): health fell since last turn with no camp or visible enemy in reach, so
    // someone in a bush is hitting us (ranked: 159 of 496 champion kills on us came from a bush).
    // Treat it as a sighting: mark the spot, leave early, and walk away rather than recall.
    const float hp = me.health();
    if (hidden_response && last_health_round == round - 1 && hp < last_health - 1 && !threatened(camps, me.position())) {
        hidden_round = round;
        mark_danger(me.position(), round);
    }
    last_health = hp;
    last_health_round = round;
    const bool hidden_threat = hidden_response && round - hidden_round <= 2;

    // The duo leaves early only under champion threat: against camps that just slowed it down.
    const bool enemy_seen = !(cambc::vision::champion_tiles(team::enemy) & cambc::vision::visible_tiles()).is_empty();
    if (enemy_seen || hidden_threat) last_enemy_round = round;
    float leave_at = (squad == role::gem_duo && enemy_seen) || hidden_threat ? duo_recall_health : recall_health;
    if (me.gem_count() > 0 && last_enemy_round >= 0 && round - last_enemy_round <= threat_memory)
        leave_at = std::max(leave_at, carrier_recall_health);
    // v7: at a sieged monster group, leave while we can still walk out of its range: its damage (scaled to
    // what is left) split over the allies with us, for `siege_escape_turns` turns.
    float siege_leave = 0;
    if (const auto* g = camps.group(current_target); g && g->siege_target && g->monolith
        && distance(me.position(), g->anchor) <= camp_range(cambc::camp_type::monolith)) {
        // v8: the Monolith hits the nearest champion. Keep enough health to walk out of range, or one more
        // hit when an ally stands closer to it (it switches to them as we step back).
        const int d = distance(me.position(), g->anchor);
        bool covered = false;
        for (const auto& ally : visible_champions(team::ally))
            covered |= !(ally.unit == me) && distance(ally.position, g->anchor) < d;
        const int exit = covered ? 1 : camp_range(cambc::camp_type::monolith) + 1 - d;
        siege_leave = (exit + 1) * g->damage;
        // Stay to finish it when it dies before we would: leaving early hands a shared Monolith to the
        // other team (01gold-rush).
        int with_us = 1;
        for (const auto& ally : visible_champions(team::ally))
            with_us += !(ally.unit == me) && distance(ally.position, g->anchor) <= camp_range(cambc::camp_type::monolith);
        const float kill_turns = g->remaining / (own.damage + body_damage * (with_us - 1));
        if (kill_turns < me.health() / std::max(1.f, g->damage) - 1)
            siege_leave = 0;
    } else if (const auto* g = camps.group(current_target); g && g->siege_target && !g->monolith
        && distance(me.position(), g->anchor) <= 5) {
        int with_us = 1;
        for (const auto& ally : visible_champions(team::ally))
            with_us += !(ally.unit == me) && distance(ally.position, g->anchor) <= 5;
        siege_leave = siege_escape_turns * g->damage * (g->remaining / std::max(1.f, g->health)) / with_us;
    }
    if (!at_spawn && (me.health() < leave_at * me.max_health() || me.health() < siege_leave)) {
        // Damage cancels a recall: step out of reach first.
        if (hidden_threat || threatened(camps, me.position())) retreat();
        else me.recall();
        return;
    }
    if (hidden_threat && !at_spawn) {
        retreat();
        return;
    }
    // Spawn heals every round: top up before heading back out.
    if (at_spawn && me.health() < spawn_leave_health * me.max_health())
        return;

    cast_support();
    place_lock();
    if (fight_enemies(round)) {
        claim = current_target;
        watch_progress(round);
        return;
    }
    if (should_tribute(round)) {
        go_tribute();
        return;
    }
    if (shop_trip())
        return;

    const int target = choose_target(round);
    if (round % 100 == 0) {
        const auto p = me.position();
        std::cout << "status r" << round << ' ' << cambc::to_string(me.champion_name()) << " at " << p.x << ',' << p.y
                  << " gems=" << me.gem_count() << " gold=" << me.gold_count() << " hp=" << int(me.health()) << " dmg=" << int(own.damage)
                  << " target=" << target << " claims=";
        const auto word = read_memory();
        for (int s = 0; s < max_bodies; ++s)
            std::cout << claim_of(word, s) << (s + 1 < max_bodies ? "," : "");
        if (const auto* g = camps.group(target))
            std::cout << " (" << g->anchor.x << ',' << g->anchor.y << " ready=" << g->reward_ready
                      << " respawn=" << g->respawn_round << " waited=" << (waiting_for == target ? waited : 0) << ')';
        std::cout << '\n';
    }
    if (target >= 0) {
        claim = target;
        work_group(target, round);
        watch_progress(round);
    }
}

// Give up on a target we stop making progress on: neither closer to it nor hitting it for
// `stall_turns` turns spent working or fighting near it. On interlock a body walked towards its
// target, met an idle enemy, fell back, and repeated that for 200 rounds.
void Bot::watch_progress(int round) {
    auto* group = camps.group(current_target);
    if (!group) {
        progress_target = -1;
        return;
    }
    const auto pos = cambc::self().position();
    // Walking distance to the target's tiles (v7): Chebyshev distance stays flat while walking a lane
    // around holes (pilgrimage: 218 false give-ups in 7 ranked games). One field per target change.
    if (progress_field_target != current_target) {
        cambc::bitboard visited;
        progress_field.compute(terrain_blocked(), group->tiles, visited);
        progress_field_target = current_target;
    }
    const int walk = progress_field.get(pos);
    const int d = walk == cambc::distance_field::unreachable ? distance(pos, group->anchor) : walk;
    // Restart after any break (tribute, recall, shopping): the walk back starts from scratch.
    const bool continued = current_target == progress_target && round == progress_round + 1;
    progress_round = round;
    if (!continued) {
        progress_target = current_target;
        best_distance = d;
        stalled = 0;
    } else if (hit_target || d < best_distance || group->known_respawning(round)
               || (group->reward_ready && group->tiles.is_set(pos))) {
        best_distance = std::min(best_distance, d);
        stalled = 0;
    } else if (++stalled > stall_turns) {
        std::cout << "stalled r" << round << " on group " << current_target << " at " << pos.x << ',' << pos.y << '\n';
        group->skip_until = round + give_up_rounds;
        current_target = claim = progress_target = -1;
    }
    hit_target = false;
}
