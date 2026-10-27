// Champion decisions. main.cpp dispatches here after the draft.
//
// Every turn: shop, track damage, survive (retreat/recall), bank gems, then run
// this champion's role. Roles fight when engaged and farm in formation otherwise.
#include <cambc.h>
#include "bot.h"
#include "team.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using cambc::bitboard;
using cambc::team;
using cambc::vec2;

// Unspent gold at which a champion recalls to shop.
constexpr std::uint32_t shop_trip_gold = 150;

// Health share at which Ryth / everyone else retreats.
constexpr float retreat_tank = 0.3f;
constexpr float retreat_other = 0.35f;
// Value bonus for staying on the current camp target.
constexpr double stick_bonus = 1.3;

// A back-liner farms a gem cluster on its own within this many tiles.
constexpr int solo_cluster_radius = 16;

// Join a cleared camp's capture (to share its gold) from this many tiles away.
// Tested 4 / 6 / 8: 345 / 351 / 348 of 380 league games; without joining: 301.
constexpr int join_capture_range = 6;

// Fall back instead of fighting when nearby enemies are this much stronger.
constexpr float outmatched_ratio = 1.3f;

// Game phases (the user's plan). Early: farm and feed Stellar, only push away
// enemies that come into reach. Mid (Stellar or Varg owns an emblem): hit
// enemies within mid_leash, then go back to farming. Late (Stellar owns three
// emblems): fight as before. Early and mid stay out of the enemy's half and
// back off when outnumbered by 2.
enum class Phase { early, mid, late };
constexpr std::uint32_t mid_game_by = 600;  // round: mid game even without an emblem
constexpr int mid_leash = 5;                // mid game: engage enemies this close
constexpr int deep_margin = 4;              // enemy half: this much closer to their spawn
constexpr int outnumbered_by = 2;           // enemies - allies within 6 tiles -> back off
constexpr int stellar_join_range = 8;       // Stellar walks this far to take a capture's gold
constexpr int capture_range = 10;           // early game: Stellar / Varg take cleared groups this close
constexpr double early_gold_weight = 2.0;   // early game: a gold coin counts as two gems in camp choice


namespace {

extern bool contest_map_active;
extern bool swirl_counterdraft_active;
extern bool swirl_team_b_active;

// ---------------------------------------------------------------- basics

int dist(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

// Tiles within Chebyshev radius r of c.
bitboard square(vec2 c, int r) {
    bitboard tiles;
    tiles.clear();
    if (r >= 0)
        tiles.draw_chebyshev(c, static_cast<float>(r));
    return tiles;
}

// Tiles whose Chebyshev distance from c is between lo and hi.
bitboard ring(vec2 c, int lo, int hi) {
    return square(c, hi) & ~square(c, lo - 1);
}

// Run an SDK action; a rejected action is logged and reported as "did nothing"
// so the rest of the turn still runs.
template<class F>
bool attempt(F&& action) {
    try {
        return action();
    } catch (const std::exception& error) {
        std::cout << "bot error: " << error.what() << '\n';
        return false;
    }
}

bool alive(const cambc::unit& u) {
    try {
        return u.is_visible() && u.health() > 0;
    } catch (const std::exception&) {
        return false;
    }
}

float health_pct(const cambc::unit& u) {
    try {
        return u.max_health() > 0 ? u.health() / u.max_health() : 1.0f;
    } catch (const std::exception&) {
        return 1.0f;
    }
}

// A unit killed (or gone out of sight) earlier this turn throws on every read;
// an uncaught throw costs the champion the rest of its turn. These helpers
// read safely.
vec2 safe_pos(const cambc::unit& u, vec2 fallback) {
    try {
        return u.position();
    } catch (const std::exception&) {
        return fallback;
    }
}

// Building champion lists is expensive (~10k+ fuel each), so the lists are
// memoised per (round, position): vision only changes between rounds or when
// this champion moves. Live stats (health, position) are still read from the
// handles on use.
template<class Build>
std::vector<cambc::champion>& memo_champions(Build build, std::uint32_t& round, vec2& pos,
                                               std::vector<cambc::champion>& cache) {
    const auto here = cambc::self().position();
    const auto now = cambc::round_number();
    if (now != round || here != pos) {
        cache = build();
        round = now;
        pos = here;
    }
    return cache;
}

const std::vector<cambc::champion>& visible_enemies() {
    static std::uint32_t round = 0xFFFFFFFF;
    static vec2 pos{-1, -1};
    static std::vector<cambc::champion> cache;
    auto& list = memo_champions([] {
        std::vector<cambc::champion> out;
        for (const auto& e : cambc::vision::champions(team::enemy))
            if (alive(e))
                out.push_back(e);
        return out;
    }, round, pos, cache);
    std::erase_if(list, [](const auto& e) { return !alive(e); }); // killed this turn
    return list;
}

// Visible living allies, not including this champion.
const std::vector<cambc::champion>& visible_allies() {
    static std::uint32_t round = 0xFFFFFFFF;
    static vec2 pos{-1, -1};
    static std::vector<cambc::champion> cache;
    return memo_champions([] {
        std::vector<cambc::champion> out;
        const auto& me = cambc::self();
        for (const auto& a : cambc::vision::champions(team::ally))
            if (!(a == me) && alive(a))
                out.push_back(a);
        return out;
    }, round, pos, cache);
}

// How much damage an enemy can deal: basic attacks plus spell power.
// Focus fire: damage it deals (plus the gems a kill would take) per point of
// health left, so the whole team finishes the cheapest dangerous kill first.
float threat(const cambc::champion& e) {
    float gems = 0;
    try {
        gems = static_cast<float>(e.gem_count());
    } catch (const std::exception&) {
    }
    try {
        const float damage = e.strength() + 0.5f * e.focus() + 2.0f * gems;
        static const bool packs_map = [] {
            if (cambc::map::width() != 31 || cambc::map::height() != 31)
                return false;
            const auto& altars = cambc::map::altars();
            return altars.is_set(vec2{2, 6}) && altars.is_set(vec2{28, 24});
        }();
        // On Packs, focus the enemy with the greatest raw damage stats first.
        return packs_map ? damage : damage / std::max(50.0f, e.health());
    } catch (const std::exception&) {
        return -1.0f;
    }
}

// Greatest damage threat, optionally only among enemies within max_range of us.
std::optional<cambc::champion> top_threat(const std::vector<cambc::champion>& enemies,
                                          int max_range = 1000) {
    const auto me = cambc::self().position();
    std::optional<cambc::champion> best;
    float best_threat = -1;
    for (const auto& e : enemies) {
        if (!alive(e) || dist(me, safe_pos(e, vec2{-1000, -1000})) > max_range)
            continue;
        const float t = threat(e);
        if (!best || t > best_threat)
            best = e, best_threat = t;
    }
    return best;
}

int nearest_enemy_distance(const std::vector<cambc::champion>& enemies, vec2 from) {
    int best = 1000;
    for (const auto& e : enemies)
        if (alive(e))
            best = std::min(best, dist(from, safe_pos(e, vec2{-1000, -1000})));
    return best;
}

// ---------------------------------------------------------------- abilities

using Abilities = std::array<cambc::ability, 2>;

template<class Pred>
const cambc::ability* find_ability(const Abilities& abilities, Pred pred) {
    for (const auto& a : abilities)
        if (pred(a))
            return &a;
    return nullptr;
}

bool is_reset(const cambc::ability& a) { return a.is_attack_reset(); }
bool is_dash(const cambc::ability& a) { return a.is_dash(); }
bool is_heal(const cambc::ability& a) { return a.is_heal(); }
bool is_shield(const cambc::ability& a) { return a.is_shield(); }
bool is_area(const cambc::ability& a) { return a.can_target_ground() && !a.is_dash(); }
bool is_single(const cambc::ability& a) {
    return (a.is_damage() || a.is_cc()) && a.can_target_enemies() && !a.can_target_ground();
}

bool ready(const cambc::ability* a) {
    return a && cambc::self().can_cast() && attempt([&] { return a->is_ready(); });
}

bool cast_self(const cambc::ability* a) {
    return ready(a) && attempt([&] { return a->use(); });
}

template<class Target>
bool cast_on(const cambc::ability* a, const Target& target) {
    return ready(a) && attempt([&] {
        return target.is_visible() && target.health() > 0
            && a->valid_tiles().is_set(target.position()) && a->use(target);
    });
}

bool cast_at(const cambc::ability* a, vec2 tile) {
    return ready(a) && attempt([&] { return a->valid_tiles().is_set(tile) && a->use(tile); });
}

// Ground tile for a radius-1 area spell that hits the most enemies. When `must`
// is given, the area has to include that tile. Returns {tile, hits}.
std::pair<std::optional<vec2>, int> best_area_tile(const cambc::ability* a,
        const std::vector<cambc::champion>& enemies, std::optional<vec2> must = std::nullopt) {
    std::optional<vec2> best;
    int best_hits = 0;
    if (!a)
        return {best, 0};
    attempt([&] {
        const auto range = a->valid_tiles();
        const auto& walls = cambc::map::walls();
        for (const auto& e : enemies)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 t = e.position() + vec2{dx, dy};
                    if (!cambc::map::is_in_bounds(t) || !range.is_set(t) || walls.is_set(t))
                        continue;
                    if (must && dist(*must, t) > 1)
                        continue;
                    int hits = 0;
                    for (const auto& other : enemies)
                        hits += dist(other.position(), t) <= 1;
                    if (hits > best_hits) {
                        best_hits = hits;
                        best = t;
                    }
                }
        return true;
    });
    return {best, best_hits};
}

// ---------------------------------------------------------------- burst
// mingstudy-ttsburst: port of ming_bot tactic/burst.cpp part 1. Whenever a
// ready damage / area / stun spell can reach an enemy champion (not one healing
// on its own spawn), cast the one that hits the most enemies, then deals the
// most damage (engine damage_amount); ties go to channelling, low-health,
// big-bag targets; a stun on an already-stunned target ranks last. Runs every
// turn before banking and the role turn, at >= 60% health.
bool burst() {
    auto& me = cambc::self();
    try {
        if (!me.can_cast() || me.health() < me.max_health() * 0.6f)
            return false;
    } catch (const std::exception&) {
        return false;
    }
    const auto& enemies = visible_enemies();
    if (enemies.empty())
        return false;
    const auto& their_spawn = cambc::map::spawn_region(team::enemy);
    const bool limited_opening = (contest_map_active || swirl_counterdraft_active)
        && cambc::round_number() < 150;
    vec2 opening_origin{};
    int opening_leash = 0;
    if (limited_opening) {
        opening_origin = me.position();
        const int minimum_leash = 5;
        opening_leash = std::max<int>(static_cast<int>(me.attack_range()), minimum_leash);
    }
    struct T {
        cambc::champion handle;
        vec2 pos;
        float hp;
        std::uint32_t gems;
        bool channel;
        bool stunned;
    };
    std::vector<T> ts;
    for (const auto& e : enemies) {
        try {
            if (!e.is_visible() || e.health() <= 0 || their_spawn.is_set(e.position()))
                continue;
            // Keep the Contest opening and the matching Swirl meta draft close
            // to each caster so ranged burst spells cannot start distant fights.
            if (limited_opening && dist(opening_origin, e.position()) > opening_leash)
                continue;
            ts.push_back({e, e.position(), e.health(), e.gem_count(),
                          e.is_submitting_gems() || e.is_recalling(), e.buffs().has_stun()});
        } catch (const std::exception&) {
        }
    }
    if (ts.empty())
        return false;
    const auto abilities = me.abilities();
    int best_slot = -1;
    std::size_t best_idx = 0;
    vec2 best_tile{};
    bool best_ground = false, best_restun = false;
    int best_hits = 0;
    float best_dmg = -1;
    const T* best_t = nullptr;
    const auto better_target = [](const T& a, const T* b) {
        if (!b)
            return true;
        if (a.channel != b->channel)
            return a.channel;
        if (a.hp != b->hp)
            return a.hp < b->hp;
        return a.gems > b->gems;
    };
    const auto consider = [&](int slot, vec2 tile, bool ground, int hits, float dmg, const T& anchor,
                              bool restun, std::size_t idx) {
        bool better;
        if (best_slot < 0) better = true;
        else if (restun != best_restun) better = !restun;
        else if (hits != best_hits) better = hits > best_hits;
        else if (dmg != best_dmg) better = dmg > best_dmg;
        else better = better_target(anchor, best_t);
        if (better) {
            best_slot = slot; best_tile = tile; best_ground = ground; best_hits = hits;
            best_dmg = dmg; best_restun = restun; best_t = &anchor; best_idx = idx;
        }
    };
    for (int s = 0; s < 2; ++s) {
        const auto& a = abilities[s];
        bitboard reach;
        bool cc = false, aoe = false, enemy_target = false, ground = false;
        try {
            if (!a.is_ready() || !(a.is_damage() || a.is_aoe_damage() || a.is_cc()))
                continue;
            cc = a.is_cc();
            aoe = a.is_aoe_damage();
            enemy_target = a.can_target_enemies();
            ground = a.can_target_ground();
            reach = a.valid_tiles();
        } catch (const std::exception&) {
            continue;
        }
        const auto dmg_of = [&](const T& t) {
            try {
                return a.damage_amount(t.handle);
            } catch (const std::exception&) {
                return 0.0f;
            }
        };
        if (enemy_target)
            for (std::size_t i = 0; i < ts.size(); ++i)
                if (reach.is_set(ts[i].pos))
                    consider(s, ts[i].pos, false, 1, dmg_of(ts[i]), ts[i], cc && ts[i].stunned, i);
        if (ground && (aoe || cc)) {
            float radius = 1.0f;
            if (aoe)
                try {
                    radius = a.get<cambc::ability_aoe_damage>().radius;
                } catch (const std::exception&) {
                }
            for (const auto& t : ts)
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy) {
                        const vec2 c{t.pos.x + dx, t.pos.y + dy};
                        if (!cambc::map::is_in_bounds(c) || !reach.is_set(c))
                            continue;
                        int hits = 0;
                        float dmg = 0;
                        bool all_stunned = true;
                        const T* anchor = nullptr;
                        std::size_t anchor_idx = 0;
                        for (std::size_t i = 0; i < ts.size(); ++i) {
                            if (dist(c, ts[i].pos) > static_cast<int>(radius))
                                continue;
                            ++hits;
                            all_stunned = all_stunned && ts[i].stunned;
                            dmg += dmg_of(ts[i]);
                            if (better_target(ts[i], anchor)) {
                                anchor = &ts[i];
                                anchor_idx = i;
                            }
                        }
                        if (hits > 0)
                            consider(s, c, true, hits, dmg, *anchor, cc && all_stunned, anchor_idx);
                    }
        }
    }
    if (best_slot < 0)
        return false;
    const auto& a = abilities[best_slot];
    if (best_ground)
        return attempt([&] { return a.use(best_tile); });
    return attempt([&] { return a.use(ts[best_idx].handle); });
}

// ---------------------------------------------------------------- attacks

template<class Target>
bool basic_attack(const Target& target) {
    auto& me = cambc::self();
    return attempt([&] {
        if (!me.can_attack() || !target.is_visible() || target.health() <= 0
            || dist(me.position(), target.position()) > me.attack_range())
            return false;
        me.attack(target);
        return true;
    });
}

// fx-lockfix: the enemy lock on an altar. cambc::vision::altar_lock_at prefers OUR
// lock when both teams locked the same altar, and attacking our own lock fails
// ("Target must be a visible enemy or neutral camp") every turn forever.
std::optional<cambc::altar_lock> enemy_lock_at(vec2 altar) {
    try {
        for (const auto& l : cambc::vision::altar_locks(team::enemy))
            if (l.position() == altar)
                return l;
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

bool attack_lock(const cambc::altar_lock& lock) {
    auto& me = cambc::self();
    return attempt([&] {
        if (!me.can_attack() || dist(me.position(), lock.position()) > me.attack_range())
            return false;
        me.attack(lock);
        return true;
    });
}

// Attack, then use an attack reset (if one is ready) to attack again.
template<class Target>
bool attack_with_reset(const Target& target, const cambc::ability* reset) {
    if (!basic_attack(target))
        return false;
    if (cast_self(reset) && !basic_attack(target))
        for (const auto& e : visible_enemies())
            if (basic_attack(e))
                break;
    return true;
}

// Basic-attack the greatest threat in range, or any enemy in range.
bool attack_enemies(const std::vector<cambc::champion>& enemies, const cambc::ability* reset) {
    auto& me = cambc::self();
    if (auto t = top_threat(enemies, me.attack_range()))
        return attack_with_reset(*t, reset);
    return false;
}

// ---------------------------------------------------------------- movement

// Terrain plus every visible unit, except this champion's own tile.
// Memoised per (round, position): occupancy only changes between rounds or
// when this champion moves, and rebuilding it costs fuel on big maps.
bitboard blocked_tiles() {
    static const bitboard terrain = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy);
    static std::uint32_t memo_round = 0xFFFFFFFF;
    static vec2 memo_pos{-1, -1};
    static bitboard memo;
    const auto here = cambc::self().position();
    const auto round = cambc::round_number();
    if (round != memo_round || here != memo_pos) {
        memo = terrain | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        memo.set(here, false);
        memo_round = round;
        memo_pos = here;
    }
    return memo;
}

// One step along a shortest path to the nearest free goal tile. A step blocked
// by a hidden unit returns false without spending the move, so try the other
// equally short directions.
bool step_towards(bitboard goals, const bitboard* avoid = nullptr) {
    auto& me = cambc::self();
    if (!me.can_move())
        return false;
    const auto here = me.position();
    auto blocked = blocked_tiles();
    if (avoid) {
        blocked |= *avoid;
        blocked.set(here, false);
    }
    goals &= ~blocked;
    if (goals.is_empty() || goals.is_set(here))
        return false;
    return attempt([&] {
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(blocked, goals, square(here, 0), visited);
        for (const auto delta : field.next_moves_from(here))
            if (!blocked.is_set(here + delta) && me.move(delta))
                return true;
        return false;
    });
}

// Move to a tile between lo and hi tiles from `target`.
bool keep_distance(vec2 target, int lo, int hi) {
    const auto here = cambc::self().position();
    const int d = dist(here, target);
    if (d >= lo && d <= hi)
        return false;
    return step_towards(ring(target, lo, hi));
}

// ---------------------------------------------------------------- camps

// Static facts about each camp group, read from the engine once per bot.
// (Asking the engine for every group every turn used up the fuel budget.)
struct GroupInfo {
    bitboard tiles;
    std::vector<vec2> spawns;
    bool wolf = false;
    bool monolith = false;
    int gems = 0;
    cambc::map::camp_group group;
    int gold = 0;
    int health = 0; // total health of every camp in the group
    bool passive = true; // only gem/gold clusters: never fights back
    int strength = 0; // total damage per turn of the group's camps
};

const std::vector<GroupInfo>& groups() {
    static const std::vector<GroupInfo> cache = [] {
        std::vector<GroupInfo> out;
        for (const auto& g : cambc::map::camp_groups()) {
            GroupInfo info{g.spawn_tiles(), {}, false, false, static_cast<int>(g.gem_value()), g};
            info.spawns = info.tiles.positions();
            info.gold = static_cast<int>(g.gold_value());
            for (const auto& c : g.camps()) {
                info.wolf |= c.is_wolf();
                info.monolith |= c.is_monolith();
                info.passive &= c.is_gem_cluster() || c.is_gold_cluster();
                info.health += c.is_monolith() ? 20000 : c.is_elk() ? 2400 : c.is_wolf() ? 1350 : 500;
                info.strength += c.is_monolith() ? 200 : c.is_elk() ? 60 : c.is_wolf() ? 20 : 0;
            }
            out.push_back(std::move(info));
        }
        return out;
    }();
    return cache;
}

const GroupInfo* group_at(vec2 tile) {
    for (const auto& g : groups())
        if (g.tiles.is_set(tile))
            return &g;
    return nullptr;
}

bool quietly(const cambc::unit& u) {
    try {
        return u.is_visible() && u.health() > 0;
    } catch (const std::exception&) {
        return false;
    }
}

// Visible, living camps belonging to the group that owns `tile`.
std::vector<cambc::camp> live_camps_of(vec2 tile) {
    std::vector<cambc::camp> out;
    const auto* group = group_at(tile);
    if (!group)
        return out;
    for (const auto& c : cambc::vision::camps())
        if (quietly(c) && group->tiles.is_set(c.spawn_position()))
            out.push_back(c);
    return out;
}

// Respawn memory: the round each group is next expected alive (0 = believed
// alive now). Refreshed from whatever this champion sees; unseen groups keep
// their last known value, so a camp captured out of sight of us is still
// believed alive, while one we saw respawning is skipped until it is back.
std::vector<int>& ready_round() {
    static std::vector<int> ready(groups().size(), 0);
    return ready;
}

// Groups whose every spawn tile is already taken by someone capturing it
// (more allies don't speed a capture up): skipped until this round.
std::vector<int>& taken_until() {
    static std::vector<int> taken(groups().size(), 0);
    return taken;
}

// Per-champion progress tracker. On the maze-like Ambush map, straight-line
// camp ranking can select a target with no navigable approach from this side.
std::size_t& stalled_group() {
    static std::size_t value = std::numeric_limits<std::size_t>::max();
    return value;
}
vec2& stalled_position() {
    static vec2 value{-1, -1};
    return value;
}
int& stalled_turns() {
    static int value = 0;
    return value;
}

bool off_limits(std::size_t i);
bool leave_to_stellar(const GroupInfo& g);
bool leave_to_carry(const GroupInfo& g);
bool stampede_map_active = false;
bool ambush_map_active = false;
bool packs_map_active = false;
bool swirl_counterdraft_active = false;
bool swirl_team_b_active = false;
bool swirl_flank_aborted = false;
bool pilgrimage_map_active = false;
bool pilgrimage_mirror_counterdraft_active = false;
bool pilgrimage_team_b_active = false;
bool contest_map_active = false;
bool collision_map_active = false;
bool corners_map_active = false;
bool rows_map_active = false;
bool interlock_map_active = false;
Phase game_phase();

// Is any of the group's spawn tiles in vision? Checks its few tiles instead of
// intersecting two full-map bitboards (that cost ~34k fuel a turn on lanes).
bool group_in_view(const GroupInfo& g, const bitboard& visible) {
    for (const auto p : g.spawns)
        if (visible.is_set(p))
            return true;
    return false;
}

void update_ledger() {
    const auto visible = cambc::vision::visible_tiles();
    const int round = static_cast<int>(cambc::round_number());
    auto& ready = ready_round();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (!group_in_view(g, visible))
            continue;
        try {
            const auto state = g.group.state();
            // mp-fixspawn: a respawning group whose timer has run out is held back by
            // someone standing on its tiles (often us, waiting for it). Treat it as one
            // round away so farm_group waits one step OFF its tiles and lets it respawn.
            if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state))
                ready[i] = round + std::max<int>(static_cast<int>(r->timer), 1);
            else
                ready[i] = round;
        } catch (const std::exception&) {
        }
    }
}

// Pick the camp group worth the most gems (plus a little gold) per turn spent
// walking there, clearing it and capturing it. The monolith hits too hard to
// be worth it unless nothing else is left, the group in `exclude` (the other
// squad's camp) is skipped, and the current choice is kept unless another is
// clearly better.
std::optional<vec2> choose_camp(vec2 from, std::optional<vec2> current,
                                std::optional<vec2> exclude = std::nullopt, bool wolves = false,
                                int helpers = -1, double gold_weight = 0) {
    const int round = static_cast<int>(cambc::round_number());
    // Health of the champion who will take the camp's hits (only checked when
    // the leader has nobody near to help).
    float my_health = 0;
    if (helpers == 0)
        try {
            my_health = cambc::self().health();
        } catch (const std::exception&) {
            helpers = -1;
        }
    std::optional<vec2> best;
    double best_value = -1;
    // Fallback when every group is respawning: the one back soonest after we
    // could walk there, so the squad waits beside it instead of wandering.
    std::optional<vec2> soonest;
    int soonest_wait = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (exclude && g.tiles.is_set(*exclude))
            continue;
        if (taken_until()[i] > round)
            continue;
        // Alone, don't start a fighting camp that would beat us (camps hit
        // the nearest champion; one champion deals about 55 a turn).
        if (helpers == 0 && !g.passive && !(current && g.tiles.is_set(*current))
            && g.strength * (g.health / 55.0f) * 0.6f > 0.7f * my_health)
            continue;
        int d = 1000;
        vec2 closest{};
        for (const auto p : g.spawns)
            if (dist(from, p) < d) {
                d = dist(from, p);
                closest = p;
            }
        // Still respawning when we would arrive: not a target yet.
        const int wait = ready_round()[i] - round - d;
        if (wait > 0) {
            if (off_limits(i))
                continue;
            if (!g.monolith && wait < soonest_wait) {
                soonest_wait = wait;
                soonest = closest;
            }
            continue;
        }
        const double turns = d + g.health / 250.0 + cambc::constants::capture_time;
        // Gems win the game; a gold-only camp is filler, picked only when no
        // gem camp is available (value 0 still beats "nothing"). Early on gold
        // counts too (gold_weight), so the nearest gold camps come first.
        double value = (g.gems + gold_weight * g.gold) / turns;
        if (g.monolith)
            value *= 0.2;
        if (wolves && g.wolf)
            value *= 1.3;
        if (current && g.tiles.is_set(*current))
            value *= rows_map_active ? 1.0 : stick_bonus;
        // Reject lower-value camps before the expensive map-side check.
        if (value <= best_value || off_limits(i))
            continue;
        best_value = value;
        best = closest;
    }
    return best ? best : soonest;
}

// Should we spend attacks on this camp? Only the farmed group, or camps that
// are already awake (damaged), so we don't wake extra groups by accident.
bool worth_hitting(const cambc::camp& c, const GroupInfo* farmed) {
    if (farmed && farmed->tiles.is_set(c.spawn_position()))
        return true;
    try {
        return c.health() < c.max_health();
    } catch (const std::exception&) {
        return false;
    }
}

std::pair<std::optional<vec2>, float> best_camp_area_tile(
        const cambc::ability* area, const std::vector<cambc::camp>& targets, float damage) {
    std::optional<vec2> best;
    float best_damage = 0;
    if (!area || damage <= 0)
        return {best, best_damage};
    attempt([&] {
        const auto range = area->valid_tiles();
        const auto& walls = cambc::map::walls();
        for (const auto& c : targets)
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 tile = c.position() + vec2{dx, dy};
                    if (!cambc::map::is_in_bounds(tile) || !range.is_set(tile) || walls.is_set(tile))
                        continue;
                    float score = 0;
                    for (const auto& other : targets)
                        if (dist(other.position(), tile) <= 1)
                            try {
                                score += std::min(damage, other.health());
                            } catch (const std::exception&) {
                            }
                    if (score > best_damage) {
                        best_damage = score;
                        best = tile;
                    }
                }
        return true;
    });
    return {best, best_damage};
}

// Spells (unless we already walked: most spells can't follow a walk), then
// basic attacks, on worthwhile camps in range.
bool hit_camps(std::optional<vec2> objective, const Abilities& abilities, bool spells = true) {
    const auto* farmed = objective ? group_at(*objective) : nullptr;
    const auto here = cambc::self().position();
    std::vector<cambc::camp> targets;
    for (const auto& c : cambc::vision::camps())
        if (dist(here, c.position()) <= 4 && quietly(c) && worth_hitting(c, farmed))
            targets.push_back(c);
    if (targets.empty())
        return false;
    // Varg's basic attack splashes to camps beside its primary target. Pick a
    // target that spreads this damage across the active camp group first.
    const bool varg_splash = cambc::self().champion_name() == cambc::champion_type::varg;
    std::vector<std::pair<vec2, int>> splash_counts;
    if (varg_splash)
        for (const auto& c : targets) {
            int hits = 0;
            for (const auto& other : targets)
                hits += dist(c.position(), other.position()) <= 1;
            splash_counts.emplace_back(c.position(), hits);
        }
    std::sort(targets.begin(), targets.end(), [&](const auto& a, const auto& b) {
        if (varg_splash) {
            const auto count_for = [&](vec2 p) {
                for (const auto& [at, count] : splash_counts)
                    if (at == p)
                        return count;
                return 0;
            };
            const int a_hits = count_for(a.position());
            const int b_hits = count_for(b.position());
            if (a_hits != b_hits)
                return a_hits > b_hits;
        }
        return a.health() < b.health();
    });

    if (spells) {
        const auto* single = find_ability(abilities, [](const auto& a) {
            return (a.is_damage() || a.is_cc()) && a.can_target_camps() && !a.can_target_ground();
        });
        const auto* area = find_ability(abilities, is_area);
        static const bool fragment_area_map = [] {
            const auto& altars = cambc::map::altars();
            return cambc::map::width() == 30 && cambc::map::height() == 30
                && !altars.is_set(vec2{26, 3}) && !altars.is_set(vec2{3, 26});
        }();
        if (!fragment_area_map) {
            for (const auto& c : targets)
                if (cast_on(single, c) || cast_at(area, c.position()))
                    break;
        } else {
            const bool single_ready = ready(single);
            const bool area_ready = ready(area);
            float single_damage = 0;
            float area_damage = 0;
            if (single_ready)
                attempt([&] { single_damage = single->damage_amount(); return true; });
            if (area_ready)
                attempt([&] { area_damage = area->damage_amount(); return true; });
            float single_value = 0;
            if (single_ready)
                for (const auto& c : targets)
                    attempt([&] {
                        if (single->valid_tiles().is_set(c.position()))
                            single_value = std::max(single_value, std::min(single_damage, c.health()));
                        return true;
                    });
            const auto [area_tile, area_value] = area_ready
                ? best_camp_area_tile(area, targets, area_damage)
                : std::pair<std::optional<vec2>, float>{std::nullopt, 0};
            bool cast = false;
            if (area_tile && area_value > single_value)
                cast = cast_at(area, *area_tile);
            if (!cast && single_ready)
                for (const auto& c : targets)
                    if (cast_on(single, c)) {
                        cast = true;
                        break;
                    }
            if (!cast && area_tile)
                cast_at(area, *area_tile);
        }
    }

    const auto* reset = find_ability(abilities, is_reset);
    for (const auto& c : targets)
        if (attack_with_reset(c, reset))
            return true;
    return false;
}

// Farm one camp group: hit it when in range, otherwise walk into attack range
// of its living camps; once it is cleared, stand on its tiles to capture.
void farm_group(vec2 target, const Abilities& abilities) {
    if (hit_camps(target, abilities))
        return;
    const auto* target_group = group_at(target);
    const auto range = cambc::self().attack_range();
    bitboard goals;
    goals.clear();
    for (const auto& c : live_camps_of(target))
        goals |= square(c.position(), range);
    if (goals.is_empty())
        if (const auto* g = target_group) {
            const auto i = static_cast<std::size_t>(g - groups().data());
            if (ready_round()[i] > static_cast<int>(cambc::round_number())) {
                // Respawning: wait one step off its tiles.
                for (const auto t : g->spawns)
                    goals |= square(t, 1);
                goals &= ~g->tiles;
            } else {
                // Only a group we can see cleared is waiting for its capture
                // (out of sight its camps may just be hidden).
                bool cleared = false;
                try {
                    cleared = group_in_view(*g, cambc::vision::visible_tiles())
                        && std::holds_alternative<cambc::map::camp_group_reward_available>(g->group.state());
                } catch (const std::exception&) {
                }
                const bool early = game_phase() == Phase::early;
                if (cleared && (early ? leave_to_carry(*g) : leave_to_stellar(*g))) {
                    // Stellar / Varg take this capture: wait beside it (early on
                    // the team moves on to the next camp meanwhile).
                    if (early)
                        taken_until()[i] = static_cast<int>(cambc::round_number()) + 6;
                    for (const auto t : g->spawns)
                        goals |= square(t, 1);
                    goals &= ~g->tiles;
                } else {
                    goals = g->tiles;
                    // Every spawn tile is taken by someone capturing it: move on.
                    if (cleared && !g->tiles.is_set(cambc::self().position())
                        && (g->tiles & ~blocked_tiles()).is_empty()) {
                        taken_until()[i] = static_cast<int>(cambc::round_number()) + 8;
                        goals.clear();
                    }
                }
            }
        }
    const auto before = cambc::self().position();
    const bool moved = step_towards(goals);
    if (target_group && cambc::map::width() == 100 && cambc::map::height() == 100) {
        const int round = static_cast<int>(cambc::round_number());
        const auto index = static_cast<std::size_t>(target_group - groups().data());
        const bool stalled = !moved && !goals.is_empty() && !goals.is_set(before)
            && !target_group->tiles.is_set(before) && ready_round()[index] <= round;
        if (!stalled) {
            stalled_group() = std::numeric_limits<std::size_t>::max();
            stalled_turns() = 0;
        } else if (stalled_group() != index || stalled_position() != before) {
            stalled_group() = index;
            stalled_position() = before;
            stalled_turns() = 1;
        } else if (++stalled_turns() >= 12) {
            taken_until()[index] = round + 100;
            stalled_group() = std::numeric_limits<std::size_t>::max();
            stalled_turns() = 0;
        }
    }
    hit_camps(target, abilities, !moved);
}

// Gem clusters never fight back (500 health, 24 gems), so a back-line champion
// farms a ready one nearby on its own instead of idling behind its leader.
bool farm_nearby_cluster(const Abilities& abilities, int radius) {
    const auto here = cambc::self().position();
    const int round = static_cast<int>(cambc::round_number());
    std::optional<vec2> best;
    int best_d = radius + 1;
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (!g.passive || g.gems == 0 || ready_round()[i] > round || taken_until()[i] > round || off_limits(i))
            continue;
        for (const auto t : g.spawns)
            if (dist(here, t) < best_d) {
                best_d = dist(here, t);
                best = t;
            }
    }
    if (!best)
        return false;
    farm_group(*best, abilities);
    return true;
}

// Rewards are split between allies standing on a cleared group's tiles when the
// capture finishes, so step onto a nearby cleared group to earn gold as well.
bool join_capture() {
    const auto here = cambc::self().position();
    const auto visible = cambc::vision::visible_tiles();
    const int range = cambc::self().champion_name() == cambc::champion_type::stellar ? stellar_join_range
                                                                                      : join_capture_range;
    for (const auto& g : groups()) {
        bool near = false;
        for (const auto p : g.spawns)
            near |= dist(here, p) <= range;
        if (!near || !group_in_view(g, visible))
            continue;
        bool cleared = false;
        try {
            cleared = std::holds_alternative<cambc::map::camp_group_reward_available>(g.group.state());
        } catch (const std::exception&) {
        }
        if (!cleared)
            continue;
        if (leave_to_stellar(g))
            continue; // Stellar takes this gold
        if (g.tiles.is_set(here))
            return true; // already capturing: stay
        if (g.gold == 0 || (g.tiles & ~blocked_tiles()).is_empty())
            continue; // nothing to share, or no free tile to stand on
        if (step_towards(g.tiles))
            return true;
    }
    return false;
}

// With nothing to farm the squads hold the middle of the map: enemies on their
// own spawn heal to full every turn, so walking in there only feeds them kills.
vec2 map_centre() {
    return {cambc::map::width() / 2, cambc::map::height() / 2};
}

// ---------------------------------------------------------------- shopping

std::vector<cambc::item_type> build_for(Role role) {
    using I = cambc::item_type;
    switch (role) {
    case Role::tank: return {I::jungle_shard_of_health, I::power_shard_of_health};
    case Role::fighter: return {I::monster_shard_of_strength, I::power_shard_of_strength};
    case Role::marksman: return {I::power_shard_of_focus};
    case Role::support: return {I::power_shard_of_focus, I::durability_shard_of_health, I::power_shard_of_focus};
    case Role::mage: return {I::power_shard_of_focus, I::durability_shard_of_health, I::power_shard_of_focus};
    case Role::farmer: return {I::monster_shard_of_strength, I::power_shard_of_strength};
    }
    return {I::power_shard_of_strength};
}

// Altar lock denial: a lock blocks the enemy team's tributes at that altar
// (ours still work) until they spend three basic attacks on it.
std::int64_t distance_to_region(vec2 p, const bitboard& region) {
    std::int64_t best = std::numeric_limits<std::int64_t>::max();
    for (const auto t : region.positions())
        best = std::min(best, p.chebyshev_distance(t));
    return best;
}

void place_lock() {
    auto& me = cambc::self();
    const auto lock = cambc::consumable_type::altar_lock;
    if (attempt([&] { return me.consumables()[lock].count() == 0; }))
        return;
    const auto enemies = visible_enemies();
    for (const auto altar : cambc::map::altars().positions()) {
        if (dist(me.position(), altar) > 2 || cambc::vision::has_altar_lock(altar))
            continue;
        const bool enemy_side = distance_to_region(altar, cambc::map::spawn_region(team::enemy))
            < distance_to_region(altar, cambc::map::spawn_region(team::ally));
        if (!enemy_side && nearest_enemy_distance(enemies, altar) > 6)
            continue;
        if (attempt([&] { return me.consumables()[lock].use(altar); }))
            return;
    }
}

// Peel (Faisal-bot's top tactic): an enemy tributing or recalling in sight is
// worth interrupting before anything else. Only a stun breaks a tribute, so
// those gems never score; any damage breaks a recall. The biggest bag with
// the most channel time left goes first. Spends at most one cast and/or the
// basic attack, so the role turn that follows still has its move.
void peel() {
    auto& me = cambc::self();
    struct Channeler {
        cambc::champion enemy;
        bool tribute;
        float worth;
    };
    std::vector<Channeler> targets;
    for (const auto& e : visible_enemies()) {
        try {
            const bool tribute = e.is_submitting_gems();
            if (!tribute && !e.is_recalling())
                continue;
            const float left = static_cast<float>(e.remaining_channel_time()) + 1.0f;
            targets.push_back({e, tribute, (static_cast<float>(e.gem_count()) + 1.0f) * left});
        } catch (const std::exception&) {
        }
    }
    if (targets.empty())
        return;
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.worth > b.worth; });

    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, [](const auto& a) {
        return a.is_cc() && a.can_target_enemies() && !a.can_target_ground();
    });
    const auto* area_stun = find_ability(abilities, [](const auto& a) {
        return a.is_cc() && a.can_target_ground() && !a.is_dash();
    });
    for (const auto& t : targets) {
        if (cast_on(stun, t.enemy) || cast_at(area_stun, t.enemy.position()))
            return;
        if (!t.tribute && basic_attack(t.enemy))
            return;
    }
}

// Shared per-turn view, so each role reads the same facts.
struct Context {
    std::vector<cambc::champion> enemies;
    std::vector<cambc::champion> allies;
    std::optional<team_memory::Objective> objective;
    std::optional<vec2> anchor; // Ryth's position, if fresh
    bool hunt = false; // this champion's squad is hunting (no camp to farm)
    std::vector<cambc::champion> threats; // every enemy in sight off its spawn; enemies = the ones to engage
};

// Fight odds: a side's strength near here is the sum of health x damage of
// its visible champions within 6 tiles. Clearly outmatched -> don't take the
// fight, fall back towards our own side instead.
float side_strength(const std::vector<cambc::champion>& side, vec2 around) {
    float total = 0;
    for (const auto& c : side) {
        try {
            if (dist(c.position(), around) <= 6)
                total += c.health() * (c.strength() + 0.5f * c.focus());
        } catch (const std::exception&) {
        }
    }
    return total;
}

bool outmatched(const std::vector<cambc::champion>& enemies, const std::vector<cambc::champion>& allies) {
    const auto& me = cambc::self();
    const auto here = me.position();
    const float ours = side_strength(allies, here) + me.health() * (me.strength() + 0.5f * me.focus());
    const float theirs = side_strength(enemies, here);
    if (swirl_counterdraft_active || contest_map_active)
        return theirs > ours; // Swirl's dense combat punished the later retreat trigger.
    const float ratio = interlock_map_active ? 1.05f : outmatched_ratio;
    return theirs > ratio * ours;
}

// Back off one step towards our spawn, away from the enemies.
void fall_back() {
    step_towards(cambc::map::spawn_region(team::ally));
}

bool lanes_team_b_side() {
    static const bool side_b = [] {
        if (cambc::map::width() != 90 || cambc::map::height() != 30)
            return false;
        const auto& ours = cambc::map::spawn_region(team::ally);
        const vec2 far_corner{cambc::map::width() - 1, cambc::map::height() - 1};
        return distance_to_region(vec2{0, 0}, ours) > distance_to_region(far_corner, ours);
    }();
    return side_b;
}

bool lanes_please_core_visible() {
    static const bool matches = [] {
        if (cambc::map::width() != 90 || cambc::map::height() != 30)
            return false;
        const auto picks = cambc::draft::current_draft(team::enemy);
        const auto has = [&](cambc::champion_draft pick) {
            return std::find(picks.begin(), picks.end(), pick) != picks.end();
        };
        return has(cambc::champion_draft::cassi_and_issac)
            && has(cambc::champion_draft::varg)
            && has(cambc::champion_draft::stellar)
            && has(cambc::champion_draft::ren)
            && has(cambc::champion_draft::tavi);
    }();
    return matches;
}

Phase game_phase() {
    if (team_memory::late_game())
        return Phase::late;
    // Keep the side-A counterdraft on one safe flank for its first camp cycle,
    // unless a nearby low-health Varg gives us a fight worth taking.
    if (swirl_counterdraft_active && !swirl_team_b_active
        && cambc::map::width() == 45 && cambc::map::height() == 30
        && cambc::round_number() < 120)
        return (swirl_flank_aborted || team_memory::swirl_abort()) ? Phase::mid : Phase::early;
    // Lanes keeps the shared first camp, then starts role-specific routes before the usual first bank.
    if (cambc::map::width() == 90 && cambc::map::height() == 30)
        return cambc::round_number() >= 50 ? Phase::mid : Phase::early;
    // Against the exact mirror, split as soon as a carry completes its first
    // emblem; this matches the opponent's early farm tempo. Keep the tested
    // round-600 opening for other Eight drafts.
    if (cambc::map::width() == 41 && cambc::map::height() == 30) {
        static const bool eight_mirror = [] {
            const auto picks = cambc::draft::current_draft(team::enemy);
            const auto has = [&](cambc::champion_draft pick) {
                return std::find(picks.begin(), picks.end(), pick) != picks.end();
            };
            return has(cambc::champion_draft::cassi_and_issac)
                && has(cambc::champion_draft::ren)
                && has(cambc::champion_draft::stellar)
                && has(cambc::champion_draft::ryth)
                && has(cambc::champion_draft::varg);
        }();
        if (eight_mirror && team_memory::mid_game())
            return Phase::mid;
        return cambc::round_number() >= mid_game_by ? Phase::mid : Phase::early;
    }
    // Interlock's mirrored top-1 team clears its first camp by round 9;
    // route the three farm groups separately from the opening instead of
    // waiting for the whole team to finish the shared early objective.
    if (interlock_map_active)
        return Phase::mid;
    if (rows_map_active)
        return Phase::mid;
    // Expanse is a pure race with several safe home camps. Split into the
    // configured three-body squad and Varg's solo route from the first turn.
    if (cambc::map::width() == 80 && cambc::map::height() == 80)
        return Phase::mid;
    // Contest loses its early farm race when everyone follows one objective;
    // split the tank squad and fighter squad from the opening.
    if (contest_map_active)
        return Phase::mid;
    // Stampede has a single contested gem camp. Keep the full team on the
    // shared farm objective instead of splitting into independent squads
    // when the ordinary mid-game trigger fires.
    // Keep Ambush on the shared opening through round 149; the general
    // mid-game memory trigger must not start the split before this point.
    if (ambush_map_active)
        return cambc::round_number() >= 150 ? Phase::mid : Phase::early;
    if (stampede_map_active)
        return Phase::early;
    if (packs_map_active || pilgrimage_map_active
        || team_memory::mid_game()
        || cambc::round_number() >= mid_game_by
        || (cambc::map::width() == 30 && cambc::map::height() == 80))
        return Phase::mid;
    return Phase::early;
}

// Deep in the enemy's half: clearly closer to their spawn than to ours.
bool deep(vec2 t) {
    return distance_to_region(t, cambc::map::spawn_region(team::enemy)) + deep_margin
        < distance_to_region(t, cambc::map::spawn_region(team::ally));
}

// Per group (cached): is its spawn tile nearest our base deep in their half?
bool deep_group(std::size_t i) {
    static std::vector<signed char> cache(groups().size(), -1);
    if (cache[i] < 0) {
        struct RegionDistance {
            std::vector<vec2> positions;
            int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
            bool rectangular = false;
            std::int64_t operator()(vec2 p) const {
                if (rectangular) {
                    const int dx = p.x < min_x ? min_x - p.x : p.x > max_x ? p.x - max_x : 0;
                    const int dy = p.y < min_y ? min_y - p.y : p.y > max_y ? p.y - max_y : 0;
                    return std::max(dx, dy);
                }
                std::int64_t best = std::numeric_limits<std::int64_t>::max();
                for (const auto t : positions)
                    best = std::min(best, static_cast<std::int64_t>(p.chebyshev_distance(t)));
                return best;
            }
        };
        const auto make_region = [](const bitboard& tiles) {
            RegionDistance out;
            out.positions = tiles.positions();
            if (!out.positions.empty()) {
                out.min_x = out.max_x = out.positions.front().x;
                out.min_y = out.max_y = out.positions.front().y;
                for (const auto p : out.positions) {
                    out.min_x = std::min(out.min_x, p.x);
                    out.max_x = std::max(out.max_x, p.x);
                    out.min_y = std::min(out.min_y, p.y);
                    out.max_y = std::max(out.max_y, p.y);
                }
                const auto area = (out.max_x - out.min_x + 1) * (out.max_y - out.min_y + 1);
                out.rectangular = area == static_cast<int>(out.positions.size());
            }
            return out;
        };
        static const auto ours = make_region(cambc::map::spawn_region(team::ally));
        static const auto theirs = make_region(cambc::map::spawn_region(team::enemy));
        std::int64_t best = std::numeric_limits<std::int64_t>::max();
        vec2 closest{};
        for (const auto t : groups()[i].spawns)
            if (const auto d = ours(t); d < best) {
                best = d;
                closest = t;
            }
        cache[i] = theirs(closest) + deep_margin < best ? 1 : 0;
    }
    return cache[i] == 1;
}

// Camps we stay away from before the late game.
bool off_limits(std::size_t i) {
    // Avoid the repeated-death enemy-side triple-elk opening tested on Collision.
    if (collision_map_active && groups()[i].strength > 100)
        return true;
    // On Interlock, the Cassi/Issac pair repeatedly died while clearing an
    // elk camp; leave those camps to the three-person squad and keep the pair
    // on passive clusters and wolves instead.
    if (interlock_map_active && groups()[i].strength >= 60) {
        const auto name = cambc::self().champion_name();
        if (name == cambc::champion_type::cassi || name == cambc::champion_type::issac)
            return true;
    }
    // Contest losses often spend the first half farming these high-DPS groups,
    // then give the opponent time to bank and lock the central altar.
    if (contest_map_active && game_phase() != Phase::late && groups()[i].strength > 100)
        return true;
    // Stampede has safe side camps but a dangerous central gem camp. Clear
    // nearby side camps first, then regroup for the central objective.
    if (stampede_map_active && cambc::round_number() < 150 && groups()[i].strength > 100)
        return true;
    return game_phase() != Phase::late && deep_group(i);
}

std::optional<vec2> stellar_position() {
    for (const auto& a : visible_allies())
        if (alive(a) && a.champion_name() == cambc::champion_type::stellar)
            return safe_pos(a, vec2{-100, -100});
    return std::nullopt;
}

// Gold goes to Stellar: when she is near a cleared group that pays gold,
// Varg leaves it to her and at most one other teammate shares it with her.
bool leave_to_stellar(const GroupInfo& g) {
    auto& me = cambc::self();
    const auto mine = me.champion_name();
    if (g.gold == 0 || mine == cambc::champion_type::stellar)
        return false;
    const auto s = stellar_position();
    if (!s)
        return false;
    bool near = false;
    for (const auto t : g.spawns)
        near |= dist(*s, t) <= 6;
    if (!near)
        return false;
    if (mine == cambc::champion_type::varg)
        return true;
    if (g.tiles.is_set(me.position()))
        return false; // already sharing it
    int others = 0;
    for (const auto& a : visible_allies())
        others += alive(a) && a.champion_name() != cambc::champion_type::stellar
            && g.tiles.is_set(safe_pos(a, vec2{-100, -100}));
    return others >= 1;
}

// Early game: Stellar and Varg take all the team's captures (rewards are split
// between the allies standing on the group, so the gold all goes to them).
bool is_capturer(cambc::champion_type t) {
    return t == cambc::champion_type::stellar || t == cambc::champion_type::varg;
}

std::optional<vec2> ally_position(cambc::champion_type t) {
    for (const auto& a : visible_allies())
        if (alive(a) && a.champion_name() == t)
            return safe_pos(a, vec2{-100, -100});
    return std::nullopt;
}

bool near_group(const GroupInfo& g, vec2 p, int range) {
    for (const auto t : g.spawns)
        if (dist(p, t) <= range)
            return true;
    return false;
}

// Stellar is free to capture unless she already stands on a group's tiles.
bool stellar_free_near(const GroupInfo& g) {
    const auto s = ally_position(cambc::champion_type::stellar);
    return s && !group_at(*s) && near_group(g, *s, 6);
}

// Early game: stay off this cleared group's tiles, a carry captures it
// (Varg leaves it to a free Stellar).
bool leave_to_carry(const GroupInfo& g) {
    const auto mine = cambc::self().champion_name();
    if (mine == cambc::champion_type::stellar)
        return false;
    if (mine == cambc::champion_type::varg)
        return stellar_free_near(g);
    for (const auto t : {cambc::champion_type::stellar, cambc::champion_type::varg})
        if (const auto p = ally_position(t); p && near_group(g, *p, capture_range))
            return true;
    return false;
}

// Early game, Stellar / Varg: capture the nearest cleared group near the team
// that the other carry is not already taking. True while doing so.
bool capture_duty() {
    auto& me = cambc::self();
    const auto mine = me.champion_name();
    if (!is_capturer(mine))
        return false;
    const auto here = me.position();
    const auto visible = cambc::vision::visible_tiles();
    const auto partner = ally_position(mine == cambc::champion_type::stellar ? cambc::champion_type::varg
                                                                              : cambc::champion_type::stellar);
    const GroupInfo* best = nullptr;
    int best_d = capture_range + 1;
    for (const auto& g : groups()) {
        if (!group_in_view(g, visible))
            continue;
        int d = 1000;
        for (const auto t : g.spawns)
            d = std::min(d, dist(here, t));
        if (d > capture_range)
            continue;
        bool cleared = false;
        try {
            cleared = std::holds_alternative<cambc::map::camp_group_reward_available>(g.group.state());
        } catch (const std::exception&) {
        }
        if (!cleared)
            continue;
        if (g.tiles.is_set(here))
            return true; // capturing: stay
        if (partner && g.tiles.is_set(*partner))
            continue; // the other carry has it
        if (mine == cambc::champion_type::varg && stellar_free_near(g))
            continue; // Stellar's
        if ((g.tiles & ~blocked_tiles()).is_empty())
            continue;
        if (d < best_d) {
            best_d = d;
            best = &g;
        }
    }
    if (!best)
        return false;
    step_towards(best->tiles);
    return true;
}

// Squad A follows Ryth's position; squad B follows the objective its leader shares.
Context read_context(bool squad_a) {
    Context ctx;
    ctx.enemies = visible_enemies();
    ctx.allies = visible_allies();
    ctx.objective = team_memory::objective();
    const auto anchor = team_memory::anchor();
    if (anchor)
        ctx.anchor = anchor->tile;
    ctx.hunt = squad_a && (game_phase() != Phase::early || corners_map_active) ? (anchor && anchor->hunt)
                                                       : (ctx.objective && ctx.objective->hunt);
    // Enemies on their own spawn heal to full every turn: never engage them.
    const auto& their_spawn = cambc::map::spawn_region(team::enemy);
    std::erase_if(ctx.enemies, [&](const auto& e) { return their_spawn.is_set(safe_pos(e, vec2{-1, -1})); });
    ctx.threats = ctx.enemies;
    // Who to engage depends on the phase: early only enemies already in
    // reach (push them away, no chasing); mid those within the leash and not
    // deep in their half; late everyone in sight.
    const auto phase = game_phase();
    const auto combat_phase = ((pilgrimage_map_active && cambc::round_number() < mid_game_by)
            || (contest_map_active && cambc::round_number() < 150)
            || (packs_map_active && cambc::round_number() < 600))
            && phase != Phase::late
        ? Phase::early : phase;
    if (combat_phase != Phase::late) {
        auto& me = cambc::self();
        const auto here = me.position();
        const int reach = contest_map_active && combat_phase == Phase::early
            ? std::max<int>(static_cast<int>(me.attack_range()), 5)
            : combat_phase == Phase::early
                ? std::max<int>(static_cast<int>(me.attack_range()), 2) : mid_leash;
        std::erase_if(ctx.enemies, [&](const auto& e) {
            const auto at = safe_pos(e, vec2{-1000, -1000});
            return dist(here, at) > reach || (combat_phase == Phase::mid && deep(at));
        });
    }
    return ctx;
}

// Back off? Clearly outmatched in strength (any phase), or, before the late
// game, outnumbered by 2+ within 6 tiles.
bool should_retreat(const Context& ctx) {
    if (ctx.threats.empty())
        return false;
    const auto here = cambc::self().position();
    const bool danger_map = interlock_map_active;
    static const bool corners_mirror_draft = [] {
        if (!corners_map_active)
            return false;
        const auto picks = cambc::draft::current_draft(team::enemy);
        const auto has = [&](cambc::champion_draft pick) {
            return std::find(picks.begin(), picks.end(), pick) != picks.end();
        };
        return has(cambc::champion_draft::cassi_and_issac)
            && has(cambc::champion_draft::ren) && has(cambc::champion_draft::stellar)
            && has(cambc::champion_draft::ryth) && has(cambc::champion_draft::varg);
    }();
    const int outmatched_retreat_range = (danger_map || corners_mirror_draft
        || (pilgrimage_team_b_active && !pilgrimage_mirror_counterdraft_active)) ? 6 : 4;
    if (outmatched(ctx.threats, ctx.allies)
        && nearest_enemy_distance(ctx.threats, here) <= outmatched_retreat_range)
        return true;
    if (game_phase() == Phase::late)
        return false;
    int enemies = 0, allies = 1;
    for (const auto& e : ctx.threats)
        enemies += alive(e) && dist(here, safe_pos(e, vec2{-1000, -1000})) <= 6;
    for (const auto& a : ctx.allies)
        allies += alive(a) && dist(here, safe_pos(a, vec2{-1000, -1000})) <= 6;
    return enemies - allies >= (danger_map ? 1 : outnumbered_by);
}

// Fall back to the nearest teammate who is not under attack, else to base.
void retreat(const Context& ctx) {
    const auto here = cambc::self().position();
    std::optional<vec2> best;
    int best_d = 1000;
    for (const auto& a : ctx.allies) {
        if (!alive(a))
            continue;
        const auto at = safe_pos(a, vec2{-1000, -1000});
        if (nearest_enemy_distance(ctx.threats, at) <= 4 || dist(here, at) >= best_d)
            continue;
        best_d = dist(here, at);
        best = at;
    }
    if (best && best_d > 2)
        step_towards(ring(*best, 1, 2));
    else
        fall_back();
}

// Tiles 1-2 steps from the leader that are not closer to his target than he
// is: followers stay behind him, so they never block his way in.
bitboard behind(vec2 leader, vec2 toward) {
    const auto around = ring(leader, 1, 2);
    const int d = dist(leader, toward);
    bitboard keep;
    keep.clear();
    for (const auto t : around.positions())
        if (dist(t, toward) >= d)
            keep.set(t, true);
    return keep.is_empty() ? around : keep;
}

// Early game team farming: the carries capture what the team cleared; the
// others work the leader's camp, staying behind him until its camps are awake
// (so they hit him first). own_target: the leader's camp, or our own when
// nobody leads.
void team_farm(const Context& ctx, const Abilities& abilities, std::optional<vec2> own_target) {
    if (capture_duty())
        return;
    if (own_target) {
        farm_group(*own_target, abilities);
        return;
    }
    if (ctx.objective && !ctx.objective->hunt) {
        const auto tile = ctx.objective->tile;
        bool awake = true;
        if (const auto camps = live_camps_of(tile); !camps.empty()) {
            awake = false;
            for (const auto& c : camps)
                try {
                    awake |= c.health() < c.max_health();
                } catch (const std::exception&) {
                }
        }
        if (awake || !ctx.anchor) {
            farm_group(tile, abilities);
            return;
        }
        if (hit_camps(std::nullopt, abilities))
            return;
        step_towards(behind(*ctx.anchor, tile));
        return;
    }
    if (ctx.objective) { // no camp to farm: hold the middle together
        step_towards(ring(ctx.objective->tile, 2, 4));
        return;
    }
    if (ctx.anchor)
        step_towards(ring(*ctx.anchor, 1, 2));
}

} // namespace

const char* champion_label(cambc::champion_type t); // scouting section below

// ==================================================================== Bot

void Bot::init() {
    initialised = true;
    interlock_map_active = cambc::map::width() == 40 && cambc::map::height() == 40;
    ambush_map_active = cambc::map::width() == 100 && cambc::map::height() == 100;
    stampede_map_active = cambc::map::width() == 60 && cambc::map::height() == 50;
    const auto& collision_altars = cambc::map::altars();
    collision_map_active = cambc::map::width() == 41 && cambc::map::height() == 41
        && collision_altars.is_set(vec2{27, 20});
    const auto& packs_altars = cambc::map::altars();
    packs_map_active = cambc::map::width() == 31 && cambc::map::height() == 31
        && packs_altars.is_set(vec2{2, 6}) && packs_altars.is_set(vec2{28, 24});
    const auto& corners_altars = cambc::map::altars();
    corners_map_active = cambc::map::width() == 71 && cambc::map::height() == 71
        && corners_altars.is_set(vec2{35, 34});
    const auto& contest_altars = cambc::map::altars();
    contest_map_active = cambc::map::width() == 50 && cambc::map::height() == 50
        && contest_altars.is_set(vec2{25, 24}) && contest_altars.is_set(vec2{24, 25});
    rows_map_active = cambc::map::width() == 50 && cambc::map::height() == 50
        && cambc::map::altars().is_set(vec2{15, 16});
    const auto& pilgrimage_altars = cambc::map::altars();
    pilgrimage_map_active = cambc::map::width() == 50 && cambc::map::height() == 65
        && pilgrimage_altars.is_set(vec2{14, 3}) && pilgrimage_altars.is_set(vec2{35, 3})
        && pilgrimage_altars.is_set(vec2{5, 58}) && pilgrimage_altars.is_set(vec2{44, 58});
    pilgrimage_team_b_active = pilgrimage_map_active
        && cambc::map::spawn_region(team::ally).is_set(vec2{46, 1});
    pilgrimage_mirror_counterdraft_active = false;
    if (pilgrimage_map_active) {
        const auto enemies = cambc::draft::current_draft(team::enemy);
        const auto has = [&](cambc::champion_draft pick) {
            return std::find(enemies.begin(), enemies.end(), pick) != enemies.end();
        };
        pilgrimage_mirror_counterdraft_active = has(cambc::champion_draft::cassi_and_issac)
            && has(cambc::champion_draft::varg) && has(cambc::champion_draft::stellar)
            && has(cambc::champion_draft::vendetta) && has(cambc::champion_draft::tavi);
    }
    const bool swirl = cambc::map::width() == 45 && cambc::map::height() == 30;
    swirl_team_b_active = swirl
        && cambc::map::spawn_region(team::ally).is_set(vec2{16, 21});
    if (swirl) {
        const auto enemies = cambc::draft::current_draft(team::enemy);
        const auto has = [&](cambc::champion_draft pick) {
            return std::find(enemies.begin(), enemies.end(), pick) != enemies.end();
        };
        swirl_counterdraft_active = has(cambc::champion_draft::cassi_and_issac)
            && has(cambc::champion_draft::varg) && has(cambc::champion_draft::stellar)
            && has(cambc::champion_draft::vendetta);
    } else {
        swirl_counterdraft_active = false;
    }
    auto& me = cambc::self();
    // Squad A: Ryth (leads), Tavi, Stellar. Squad B: Issac (leads), Cassi, Prism.
    using T = cambc::champion_type;
    switch (me.champion_name()) {
    case T::ryth: role = Role::tank; squad_a = true; break;
    case T::tavi: role = Role::marksman; squad_a = true; break;
    case T::stellar: role = Role::mage;
        squad_a = !(cambc::map::width() == 80 && cambc::map::height() == 80); break;
    case T::willow: role = Role::support; squad_a = true; break;
    case T::issac: role = Role::fighter; squad_a = false; break;
    case T::vendetta: role = Role::fighter; squad_a = false; break;
    case T::ren: {
        const auto allies = cambc::champions(team::ally);
        const bool has_ryth = std::any_of(allies.begin(), allies.end(), [](const auto& ally) {
            return ally.champion_name() == T::ryth;
        });
        // Ren fills the tank slot on three-pick teams that do not draft Ryth.
        role = has_ryth ? Role::fighter : Role::tank;
        squad_a = !has_ryth;
        break;
    }
    case T::cassi: role = Role::mage; squad_a = false; break;
    case T::prism: role = Role::mage; squad_a = false; break;
    case T::varg: role = Role::farmer;
        squad_a = !pilgrimage_map_active || pilgrimage_mirror_counterdraft_active; break;
    default: role = Role::fighter; squad_a = false; break;
    }
    // Higher priority acts first: the initiator, then the front line, then the back line.
    const bool five = cambc::champions(team::ally).size() >= 5;
    int priority = 30;
    switch (role) {
    case Role::mage: priority = five && me.champion_name() == cambc::champion_type::prism ? 60 : 45; break;
    case Role::tank: priority = 55; break;
    case Role::fighter: priority = 50; break;
    case Role::marksman: priority = 40; break;
    case Role::support: priority = 30; break;
    case Role::farmer: priority = 35; break;
    }
    attempt([&] { me.set_priority(priority); return true; });
    last_health = me.health();
}

void Bot::shop() {
    auto& me = cambc::self();
    using T = cambc::champion_type;
    using I = cambc::item_type;
    const auto name = me.champion_name();
    // The phase advances with the carries' emblems (5 matching shards each).
    if ((name == T::stellar || name == T::varg) && shards_bought >= 5 && !team_memory::mid_game()) {
        team_memory::set_mid_game();
        std::cout << "SCOUT PHASE " << cambc::round_number() << " mid " << champion_label(name) << std::endl;
    }
    if (name == T::stellar && shards_bought >= 15 && !team_memory::late_game()) {
        team_memory::set_late_game();
        std::cout << "SCOUT PHASE " << cambc::round_number() << " late Stellar" << std::endl;
    }
    // Stellar is the team's carry: all Focus. Varg farms: Monster Strength
    // (10% more damage to camps), then Power Strength. Issac leads squad B
    // into camps, so he builds like a tank.
    const auto& altars = cambc::map::altars();
    const bool rows_damage_map = cambc::map::width() == 50 && cambc::map::height() == 50
        && altars.is_set(vec2{15, 16});
    const bool lanes_damage_map = cambc::map::width() == 90 && cambc::map::height() == 30;
    const bool lanes_please_ryth_profile = lanes_damage_map && name == T::ryth
        && lanes_team_b_side() && lanes_please_core_visible();
    const bool expanse_damage_map = cambc::map::width() == 80 && cambc::map::height() == 80;
    const bool carry_monster_focus = rows_damage_map || lanes_damage_map || expanse_damage_map;
    const auto issac_role = rows_damage_map || lanes_damage_map ? Role::fighter : Role::tank;
    const auto stellar_build = carry_monster_focus
        ? std::vector<cambc::item_type>{I::monster_shard_of_focus, I::power_shard_of_focus}
        : std::vector<cambc::item_type>{I::power_shard_of_focus};
    const auto ryth_build = lanes_please_ryth_profile
        ? std::vector<cambc::item_type>{I::power_shard_of_strength, I::monster_shard_of_strength}
        : rows_damage_map || lanes_damage_map
        ? std::vector<cambc::item_type>{I::monster_shard_of_health, I::power_shard_of_health}
        : build_for(Role::tank);
    const std::vector<I> ren_tank_build{I::bruiser_shard_of_health, I::durability_shard_of_health,
                                         I::power_shard_of_focus};
    auto build = name == T::stellar ? stellar_build
        : name == T::varg ? std::vector<cambc::item_type>{I::monster_shard_of_strength, I::power_shard_of_strength}
        : name == T::ryth ? ryth_build
        : name == T::ren && role == Role::tank ? ren_tank_build
        : build_for(name == T::issac ? issac_role : role);
    // Side B starts slightly farther from Lanes' central camps. Test Tavi's
    // camp-damage emblem there while retaining his usual build on side A.
    static const bool lanes_side_b = [] {
        if (cambc::map::width() != 90 || cambc::map::height() != 30)
            return false;
        const auto& ours = cambc::map::spawn_region(team::ally);
        const vec2 far_corner{cambc::map::width() - 1, cambc::map::height() - 1};
        return distance_to_region(vec2{0, 0}, ours) > distance_to_region(far_corner, ours);
    }();
    if (name == T::tavi && lanes_side_b)
        build = {I::monster_shard_of_strength, I::power_shard_of_focus};
    // On the Swirl side-B counterdraft, use the top team's power/health/power
    // queue for the frontline and fighters; keep side A's farming build intact.
    if (swirl_counterdraft_active && swirl_team_b_active
        && (name == T::issac || name == T::vendetta || name == T::varg || name == T::ren))
        build = {I::power_shard_of_strength, I::power_shard_of_health,
                 I::power_shard_of_strength};
    const auto lock = cambc::consumable_type::altar_lock;
    if (me.consumables()[lock].count() == 0 && me.can_buy(lock))
        attempt([&] { me.buy(lock); return true; });
    for (int purchases = 0; purchases < 10; ++purchases) {
        const auto step = std::min<std::size_t>(shards_bought / 5, build.size() - 1);
        const auto item = build[step];
        if (me.gold_count() < item.cost() || !me.can_buy(item))
            return;
        if (!attempt([&] { me.buy(item); return true; }))
            return;
        ++shards_bought;
    }
}

// Lost health since our last turn with an enemy champion close by counts as
// being attacked by a champion: this champion may fight back for the next
// few rounds. (The engine doesn't say who hit us, so a camp hit while an
// enemy champion stands within 4 tiles also counts.)
void Bot::track_damage() {
    auto& me = cambc::self();
    const float health = me.health();
    const bool hurt = health < last_health - 0.5f;
    last_health = health;
    if (!hurt)
        return;
    const auto enemies = visible_enemies();
    if (nearest_enemy_distance(enemies, me.position()) > 4)
        return;
    if (role == Role::tank)
        team_memory::raise_fight();
    if (health < me.max_health() * 0.4f)
        team_memory::raise_danger();
}

// Survival comes first: retreat or recall when low.
bool Bot::survive() {
    auto& me = cambc::self();
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    const float threshold = role == Role::tank ? retreat_tank : retreat_other;
    if (me.health() >= me.max_health() * threshold)
        return false;
    if (at_spawn)
        return true; // stay put; spawn heals at turn end

    // Any damage cancels a recall, so first step out of reach of awake camps
    // (attack range 3, and they never chase), then recall.
    bitboard camp_reach;
    camp_reach.clear();
    for (const auto& c : cambc::vision::camps())
        if (alive(c) && c.health() < c.max_health())
            camp_reach |= square(c.position(), 3);
    if (camp_reach.is_set(me.position())) {
        step_towards(square(me.position(), 6) & ~camp_reach);
        return true;
    }

    const auto enemies = visible_enemies();
    const bool threatened = nearest_enemy_distance(enemies, me.position()) <= 5;
    if (!threatened)
        return attempt([&] { me.recall(); return true; });

    // Vendetta falls back to Willow for healing when the way is clear.
    if (role == Role::fighter)
        for (const auto& ally : visible_allies())
            if (ally.champion_name() == cambc::champion_type::willow
                && nearest_enemy_distance(enemies, ally.position()) >= 4) {
                const auto abilities = me.abilities();
                const auto* dash = find_ability(abilities, is_dash);
                for (const auto t : (ring(ally.position(), 1, 1) & ~blocked_tiles()).positions())
                    if (cast_at(dash, t))
                        return true;
                step_towards(ring(ally.position(), 1, 1));
                return true;
            }

    // Otherwise run home; recall only once out of reach.
    step_towards(cambc::map::spawn_region(team::ally));
    return true;
}

// Carried gems are banked straight away (tested best: carrying gems loses them).
// Each altar is priced by real walking distance; an enemy-locked one costs a
// few extra turns to break the lock (3 basic attacks), so a locked altar on
// our side still beats an unlocked one behind the enemy spawn. (Skipping
// locked altars left champions frozen with full bags once Faisal-bot locked
// the reachable altar.)
bool Bot::bank() {
    auto& me = cambc::self();
    if (me.gem_count() == 0)
        return false;
    const auto enemy_locks = cambc::vision::altar_lock_tiles(team::enemy);
    const auto here = me.position();
    const int range = me.attack_range();

    // Carrying gems, route around tiles within 3 of a visible enemy when an
    // altar can still be reached that way; otherwise take the direct route.
    bitboard danger;
    danger.clear();
    for (const auto& e : visible_enemies())
        if (alive(e))
            danger |= square(safe_pos(e, here), 3);
    danger.set(here, false);
    bool safe_route = !danger.is_empty();
    auto blocked = blocked_tiles();
    if (safe_route)
        blocked |= danger;

    // Distances from here to every reachable tile.
    cambc::distance_field field;
    bitboard visited;
    attempt([&] { field.compute(blocked, square(here, 0), visited); return true; });
    if (safe_route) {
        // No altar reachable without passing enemies: take the direct route.
        bool reachable = false;
        for (const auto altar : cambc::map::altars().positions())
            reachable |= !(square(altar, std::max(range, 1)) & visited).is_empty();
        if (!reachable) {
            safe_route = false;
            blocked = blocked_tiles();
            attempt([&] { field.compute(blocked, square(here, 0), visited); return true; });
        }
    }
    const auto steps_to = [&](const bitboard& tiles) {
        int best = std::numeric_limits<int>::max();
        for (const auto t : (tiles & visited).positions())
            best = std::min<int>(best, field.get(t));
        return best;
    };

    std::optional<vec2> best_altar;
    bool best_locked = false;
    int best_cost = std::numeric_limits<int>::max();
    for (const auto altar : cambc::map::altars().positions()) {
        const bool locked = enemy_locks.is_set(altar);
        if (!locked && dist(here, altar) <= cambc::constants::submit_gems_range)
            return attempt([&] { me.submit_gems(altar); return true; });
        if (locked && dist(here, altar) <= range)
            if (const auto l = enemy_lock_at(altar); l && attack_lock(*l))
                return true;
        const auto stand = locked ? square(altar, range) & ~square(altar, 0)
                                  : square(altar, cambc::constants::submit_gems_range);
        int cost = steps_to(stand);
        if (cost == std::numeric_limits<int>::max())
            continue;
        if (locked)
            cost += 4;
        // On Pilgrimage, large bags should not cross into the enemy half just
        // to use a closer altar when an allied-half route is available.
        if (pilgrimage_map_active && me.gem_count() >= 100) {
            const int west = distance_to_region(vec2{0, 1}, cambc::map::spawn_region(team::ally));
            const int east = distance_to_region(vec2{cambc::map::width() - 1, 1}, cambc::map::spawn_region(team::ally));
            const bool ally_west = west < east;
            const bool enemy_half = ally_west ? altar.x >= cambc::map::width() / 2
                                               : altar.x < cambc::map::width() / 2;
            if (enemy_half)
                cost += 40;
        }
        if (cost < best_cost) {
            best_cost = cost;
            best_altar = altar;
            best_locked = locked;
        }
    }

    // Fight back on the way if a champion hurt us and we are melee (ranged
    // would lose the move).
    if (range <= 1 && fighting_back())
        attack_enemies(visible_enemies(), nullptr);
    if (!best_altar)
        return true; // no altar reachable right now (blocked in): wait
    const auto stand = best_locked ? square(*best_altar, range) & ~square(*best_altar, 0)
                                   : square(*best_altar, cambc::constants::submit_gems_range);
    step_towards(stand, safe_route ? &danger : nullptr);
    if (best_locked && dist(me.position(), *best_altar) <= range)
        if (const auto l = enemy_lock_at(*best_altar))
            attack_lock(*l);
    return true;
}

// Gold only buys upgrades on spawn, so go home to spend a full purse when it
// is safe: no gems to lose, no enemy in sight, no fight going on. Tavi shops
// anywhere, so he never needs the trip.
bool Bot::shopping_trip() {
    auto& me = cambc::self();
    std::uint32_t trip_threshold = shop_trip_gold;
    if (cambc::map::width() == 90 && cambc::map::height() == 30) {
        const auto& ours = cambc::map::spawn_region(team::ally);
        const vec2 far_corner{cambc::map::width() - 1, cambc::map::height() - 1};
        const bool lanes_side_b = distance_to_region(vec2{0, 0}, ours)
            > distance_to_region(far_corner, ours);
        if (lanes_side_b)
            trip_threshold = 250;
    }
    if (role == Role::marksman || me.gold_count() < trip_threshold || me.gem_count() > 0)
        return false;
    if (cambc::map::spawn_region(team::ally).is_set(me.position()))
        return false;
    if (team_memory::fight_alarm() || !visible_enemies().empty())
        return false;
    return attempt([&] { me.recall(); return true; });
}

// Each squad leader picks a camp. Squad B's leader shares hers as the
// objective; Ryth picks a different group and shares his position. With no
// camp left to farm, a squad holds the map centre. A follower whose leader is
// silent (dead or recalling) farms on its own.
std::optional<vec2> choose_solo_camp(vec2 from, std::optional<vec2> current,
                                     std::optional<vec2> avoid_a, std::optional<vec2> avoid_b);

void Bot::choose_objective() {
    const auto here = cambc::self().position();
    const auto vendetta_camp = team_memory::objective();
    // Allies close enough to share a fighting camp's hits.
    int helpers = 0;
    for (const auto& a : visible_allies())
        helpers += dist(safe_pos(a, vec2{-100, -100}), here) <= 5;
    const bool expanse_map = cambc::map::width() == 80 && cambc::map::height() == 80;
    const double map_opening_gold_weight = expanse_map && game_phase() != Phase::late ? 0.75
        : corners_map_active && game_phase() == Phase::early ? early_gold_weight : 0.0;
    if (swirl_counterdraft_active && !swirl_team_b_active && !swirl_flank_aborted
        && !team_memory::swirl_abort()
        && cambc::map::width() == 45 && cambc::map::height() == 30
        && cambc::round_number() < 120) {
        // The strong Swirl route trace first captured 44 gems from the
        // opposite-flank 88-gem pack at (13,13). Keep the whole team together
        // on that camp for one clear/bank cycle before normal routing resumes.
        const vec2 flank_camp{13, 13};
        farm_target = group_at(flank_camp) ? std::optional<vec2>(flank_camp)
                                           : choose_camp(here, farm_target, std::nullopt,
                                                         false, helpers, early_gold_weight);
        if (role == Role::tank)
            hunting = !farm_target;
        if (farm_target)
            team_memory::set_objective(*farm_target, false);
        return;
    }
    if (game_phase() == Phase::early && !corners_map_active) {
        // One team: Ryth leads (Issac while Ryth is dead) to the nearest gold
        // camps first; everyone else works the leader's camp.
        const double gold_weight = contest_map_active ? 0.25 : early_gold_weight;
        const bool ryth_heard = team_memory::anchor().has_value();
        const bool leads = role == Role::tank
            || (cambc::self().champion_name() == cambc::champion_type::issac && !ryth_heard);
        if (leads) {
            farm_target = choose_camp(here, farm_target, std::nullopt, false, helpers, gold_weight);
            if (farm_target)
                team_memory::set_objective(*farm_target, false);
            else
                team_memory::set_objective(map_centre(), true);
            if (role == Role::tank)
                hunting = !farm_target;
            return;
        }
        const bool leader_heard = ryth_heard || vendetta_camp.has_value();
        farm_target = leader_heard ? std::nullopt
                                   : choose_camp(here, farm_target, std::nullopt, false, -1, gold_weight);
        return;
    }
    switch (role) {
    case Role::fighter:
        farm_target = choose_camp(here, farm_target, std::nullopt, false, helpers, map_opening_gold_weight);
        if (farm_target)
            team_memory::set_objective(*farm_target, false);
        else
            team_memory::set_objective(map_centre(), true);
        break;
    case Role::tank: {
        std::optional<vec2> exclude;
        if (vendetta_camp && !vendetta_camp->hunt)
            exclude = vendetta_camp->tile;
        farm_target = choose_camp(here, farm_target, exclude, false, helpers, map_opening_gold_weight);
        hunting = !farm_target;
        break;
    }
    case Role::marksman:
    case Role::support:
    case Role::farmer:
        break; // farmer_turn picks its own camps
    case Role::mage: {
        if (expanse_map && cambc::self().champion_name() == cambc::champion_type::cassi) {
            const auto avoid = vendetta_camp && !vendetta_camp->hunt
                ? std::optional<vec2>(vendetta_camp->tile) : std::nullopt;
            farm_target = choose_solo_camp(here, farm_target, std::nullopt, avoid);
            // Share Cassi's farm lane through the otherwise unused Expanse
            // anchor slot so Varg avoids both solo routes and Issac's camp.
            team_memory::set_anchor(farm_target.value_or(map_centre()), !farm_target);
            break;
        }
        const bool leader_heard = squad_a ? team_memory::anchor().has_value() : vendetta_camp.has_value();
        farm_target = leader_heard ? std::nullopt
                                    : choose_camp(here, farm_target, std::nullopt, false, -1, map_opening_gold_weight);
        break;
    }
    }
}

// A lone wolf (1350 health, hits for 20) is safe for one healthy champion, so
// a back-liner farms one near its leader instead of trailing behind while no
// enemy has been seen for a while. Cassi stays with Issac (they die together).
bool Bot::farm_nearby_wolf(const Abilities& abilities, std::optional<vec2> leader) {
    auto& me = cambc::self();
    if (!leader || me.champion_name() == cambc::champion_type::cassi)
        return false;
    if (enemy_seen_round && cambc::round_number() <= enemy_seen_round + 20)
        return false;
    const auto here = me.position();
    const int round = static_cast<int>(cambc::round_number());
    std::optional<vec2> best;
    int best_d = 13;
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        if (g.passive || g.strength > 20 || g.gems == 0 || ready_round()[i] > round || taken_until()[i] > round
            || off_limits(i))
            continue;
        for (const auto t : g.spawns) {
            // Near the leader for help, but not the leader's own camp.
            const int dl = dist(*leader, t);
            if (dl > 10 || dl <= 3 || dist(here, t) >= best_d)
                continue;
            best_d = dist(here, t);
            best = t;
        }
    }
    if (!best)
        return false;
    // Start only when healthy; once the wolf is awake, finish it (survive()
    // still pulls out below the retreat threshold).
    bool awake = false;
    for (const auto& c : live_camps_of(*best))
        try {
            awake |= c.health() < c.max_health();
        } catch (const std::exception&) {
        }
    if (!awake && me.health() < me.max_health() * 0.75f)
        return false;
    farm_group(*best, abilities);
    return true;
}

// Solo farmer (Varg): camps one champion clears safely on its own (gem and
// gold clusters, lone wolves), valued by gems per turn of walking, clearing
// (~100 damage a turn with splash) and capturing. Groups next to a squad's
// target are left to the squad.
std::optional<vec2> choose_solo_camp(vec2 from, std::optional<vec2> current, std::optional<vec2> avoid_a,
                                     std::optional<vec2> avoid_b) {
    const int round = static_cast<int>(cambc::round_number());
    const auto& me = cambc::self();
    const bool rows_varg = rows_map_active && me.champion_name() == cambc::champion_type::varg;
    std::optional<vec2> best, soonest;
    double best_value = -1;
    int soonest_wait = std::numeric_limits<int>::max();
    for (std::size_t i = 0; i < groups().size(); ++i) {
        const auto& g = groups()[i];
        const bool rows_wolf_pack = rows_varg && g.wolf && g.strength <= 60;
        if (g.gems == 0 || g.monolith || (!g.passive && g.strength > 20 && !rows_wolf_pack)
            || taken_until()[i] > round || off_limits(i))
            continue;
        const bool is_current = current && g.tiles.is_set(*current);
        if (rows_wolf_pack && !is_current && health_pct(me) < 0.75f)
            continue;
        int d = 1000;
        vec2 closest{};
        for (const auto p : g.spawns)
            if (dist(from, p) < d) {
                d = dist(from, p);
                closest = p;
            }
        if (rows_varg && distance_to_region(closest, cambc::map::spawn_region(team::ally))
                > distance_to_region(closest, cambc::map::spawn_region(team::enemy)))
            continue;
        if (!is_current && ((avoid_a && dist(*avoid_a, closest) <= 3) || (avoid_b && dist(*avoid_b, closest) <= 3)))
            continue;
        const int wait = ready_round()[i] - round - d;
        if (wait > 0) {
            if (wait < soonest_wait) {
                soonest_wait = wait;
                soonest = closest;
            }
            continue;
        }
        double value = g.gems / (d + g.health / 100.0 + cambc::constants::capture_time);
        if (is_current)
            value *= stick_bonus;
        if (value > best_value) {
            best_value = value;
            best = closest;
        }
    }
    return best ? best : soonest;
}

void Bot::farmer_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* reset = find_ability(abilities, is_reset);
    const auto* shield = find_ability(abilities, is_shield);
    const bool engage = !ctx.enemies.empty();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }
    if (engage) {
        const auto target = top_threat(ctx.enemies);
        if (!target)
            return;
        const auto at = safe_pos(*target, me.position());
        if (dist(me.position(), at) <= 2)
            cast_self(shield);
        if (attack_with_reset(*target, reset))
            return;
        step_towards(ring(at, 1, 1));
        attack_with_reset(*target, reset);
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        // Shield up next to an awake fighting camp (it soaks most of the hits).
        for (const auto& c : cambc::vision::camps())
            if (quietly(c) && !c.is_gem_cluster() && !c.is_gold_cluster()
                && dist(me.position(), c.position()) <= 3 && c.health() < c.max_health()) {
                cast_self(shield);
                break;
            }
        team_farm(ctx, abilities, farm_target);
        return;
    }
    if (join_capture())
        return;
    std::optional<vec2> squad_b;
    if (ctx.objective && !ctx.objective->hunt)
        squad_b = ctx.objective->tile;
    farm_target = choose_solo_camp(me.position(), farm_target, ctx.anchor, squad_b);
    if (!farm_target) { // nothing to solo here: help Issac's squad clear its camp
        if (squad_b) {
            farm_group(*squad_b, abilities);
            return;
        }
        if (hit_camps(std::nullopt, abilities))
            return;
        if (ctx.anchor)
            step_towards(ring(*ctx.anchor, 1, 2));
        return;
    }
    // Shield up before a wolf's hits land (it soaks most of them).
    for (const auto& c : live_camps_of(*farm_target))
        if (!c.is_gem_cluster() && !c.is_gold_cluster() && dist(me.position(), c.position()) <= 3) {
            cast_self(shield);
            break;
        }
    farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- scouting
// SCOUT lines in stdout tell us what an opponent does in platform matches,
// where only our own log is available (tools/scout.py summarises them):
//   SCOUT DRAFT ...                    both rosters and the map (first turn)
//   SCOUT SEE r me@x,y gems | enemies  enemies in sight (name@x,y:gems/hp%, T tributing, R recalling)
//   SCOUT DEATH r me gems | enemies    one of ours died, its bag and who was near
//   SCOUT LOCK r x,y                   an enemy altar lock appeared
const char* champion_label(cambc::champion_type t) {
    static constexpr const char* names[] = {"Ryth", "Vesper", "Vendetta", "Cassi", "Issac", "Stellar",
                                            "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi"};
    const auto i = static_cast<int>(t);
    return i >= 0 && i < 12 ? names[i] : "?";
}

const char* draft_label(cambc::champion_draft d) {
    static constexpr const char* names[] = {"Ryth", "Vesper", "Vendetta", "Cassi&Issac", "Stellar", "Prism",
                                            "Marcella", "Willow", "Ren", "Varg", "Tavi"};
    const auto i = static_cast<int>(d);
    return i >= 0 && i < 11 ? names[i] : "?";
}

void scout(bool reporter) {
    try {
        auto& me = cambc::self();
        const auto round = cambc::round_number();
        static bool drafted = false;
        if (reporter && !drafted) {
            drafted = true;
            std::cout << "SCOUT DRAFT map " << cambc::map::width() << 'x' << cambc::map::height() << " ours";
            for (const auto d : cambc::draft::current_draft(team::ally))
                std::cout << ' ' << draft_label(d);
            std::cout << " | theirs";
            for (const auto d : cambc::draft::current_draft(team::enemy))
                std::cout << ' ' << draft_label(d);
            std::cout << std::endl;
        }
        static bool was_alive = true;
        static std::uint32_t last_gems = 0, last_seen = 0;
        static std::string last_near;
        static bitboard locks_known = [] { bitboard b; b.clear(); return b; }();
        const auto name = champion_label(me.champion_name());
        if (!me.is_alive()) {
            if (was_alive)
                std::cout << "SCOUT DEATH " << round << ' ' << name << ' ' << last_gems << " |" << last_near << std::endl;
            was_alive = false;
            return;
        }
        was_alive = true;
        last_gems = me.gem_count();
        std::string near;
        for (const auto& e : visible_enemies()) {
            if (!alive(e))
                continue;
            const auto at = safe_pos(e, vec2{-1, -1});
            near += ' ';
            near += champion_label(e.champion_name());
            near += '@' + std::to_string(at.x) + ',' + std::to_string(at.y) + ':' + std::to_string(e.gem_count()) + '/'
                + std::to_string(static_cast<int>(100 * health_pct(e)));
            if (e.is_submitting_gems())
                near += 'T';
            if (e.is_recalling())
                near += 'R';
        }
        last_near = near;
        if (!near.empty() && (last_seen == 0 || round >= last_seen + 15)) {
            last_seen = round;
            const auto here = me.position();
            std::cout << "SCOUT SEE " << round << ' ' << name << '@' << here.x << ',' << here.y << ' ' << last_gems
                      << " |" << near << std::endl;
        }
        const auto locks = cambc::vision::altar_lock_tiles(team::enemy);
        for (const auto t : (locks & ~locks_known).positions())
            std::cout << "SCOUT LOCK " << round << ' ' << t.x << ',' << t.y << std::endl;
        locks_known |= locks;
    } catch (const std::exception&) {
    }
}

// Fight any enemy champion in sight (unless clearly outmatched, see
// outmatched()). Tested overnight: fighting only after being hit lost to this
// head to head 7-13 and did worse against the Faisal-bot family.
bool Bot::fighting_back() const {
    return true;
}

// On Swirl, stop the flank-farm opening when the enemy Varg is both weakened
// and close enough for our team to convert the fight. A reserved shared-memory
// stamp broadcasts the persistent phase switch; each champion also caches it.
void detect_swirl_flank_abort() {
    if (swirl_flank_aborted || !swirl_counterdraft_active || swirl_team_b_active
        || cambc::map::width() != 45 || cambc::map::height() != 30
        || cambc::round_number() >= 120)
        return;
    const auto here = cambc::self().position();
    for (const auto& enemy : visible_enemies()) {
        if (enemy.champion_name() == cambc::champion_type::varg
            && health_pct(enemy) <= 0.60f && dist(enemy.position(), here) <= 6) {
            swirl_flank_aborted = true;
            team_memory::set_swirl_abort();
            std::cout << "SWIRL_ABORT " << cambc::round_number() << " Varg@"
                      << enemy.position().x << ',' << enemy.position().y << " hp="
                      << static_cast<int>(health_pct(enemy) * 100) << std::endl;
            return;
        }
    }
}

void Bot::play_turn() {
    if (!initialised)
        init();
    auto& me = cambc::self();

    // Score trace for tools/league.py: the first ally reports every 5 rounds.
    static const bool reporter = [&] {
        const auto allies = cambc::champions(team::ally);
        return !allies.empty() && allies.front() == me;
    }();
    if (reporter && cambc::round_number() % 5 == 0)
        std::cout << "SCORE " << cambc::round_number() << ' '
                  << cambc::global_submitted_gems(team::ally) << ' '
                  << cambc::global_submitted_gems(team::enemy) << std::endl;
    scout(reporter);

    if (!me.is_alive()) { // dead champions can still shop
        shop();
        return;
    }
    track_damage();
    detect_swirl_flank_abort();
    if (!visible_enemies().empty())
        enemy_seen_round = cambc::round_number();
    if (role == Role::tank) {
        team_memory::set_anchor(me.position(), hunting);
        if (farm_target && game_phase() == Phase::early && !corners_map_active
            && (me.is_recalling() || me.is_submitting_gems()))
            team_memory::set_objective(*farm_target, false); // the team keeps farming meanwhile
    }
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;

    shop();
    if (survive())
        return;
    update_ledger();
    choose_objective();
    if (role == Role::tank)
        team_memory::set_anchor(me.position(), hunting);
    burst(); // mingstudy-ttsburst: spend ready spells on enemies in reach first
    if (bank())
        return;
    if (shopping_trip())
        return;
    peel();
    place_lock();

    switch (role) {
    case Role::tank: tank_turn(); break;
    case Role::fighter: fighter_turn(); break;
    case Role::marksman: marksman_turn(); break;
    case Role::support: support_turn(); break;
    case Role::mage: mage_turn(); break;
    case Role::farmer: farmer_turn(); break;
    }
}

// ---------------------------------------------------------------- Ryth

void Bot::tank_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun_area = find_ability(abilities, is_area);
    const auto* reset = find_ability(abilities, is_reset);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());

    (void)nearest;
    const bool engage = !ctx.enemies.empty() && fighting_back();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto [tile, hits] = best_area_tile(stun_area, ctx.enemies);
        if (tile && hits >= 1)
            cast_at(stun_area, *tile);
        if (!attack_enemies(ctx.enemies, reset)) {
            if (const auto target = top_threat(ctx.enemies))
                keep_distance(safe_pos(*target, me.position()), 1, 1);
            attack_enemies(ctx.enemies, reset);
        }
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        team_farm(ctx, abilities, farm_target);
        return;
    }
    // Farming: keep collecting gems even with enemies in sight.
    if (hunting) {
        step_towards(square(map_centre(), 3));
        return;
    }
    // Front line: Ryth walks in first, so the camp targets him.
    if (farm_target)
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Vendetta

void Bot::fighter_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* reset = find_ability(abilities, is_reset);
    const auto* dash = find_ability(abilities, is_dash);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());

    // Vendetta keeps securing her camp during a fight unless a teammate is in
    // danger, an enemy is on top of her, or there is no camp to secure.
    (void)nearest;
    const bool engage = !ctx.enemies.empty() && fighting_back();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto target = top_threat(ctx.enemies);
        if (!target)
            return;
        if (attack_with_reset(*target, reset))
            return;
        // Flank: approach the threat from a side away from its teammates.
        bitboard flank = ring(safe_pos(*target, me.position()), 1, 1) & ~blocked_tiles();
        for (const auto& e : ctx.enemies)
            if (!(e == *target) && alive(e)) {
                const auto away = flank & ~square(safe_pos(e, me.position()), 1);
                if (!away.is_empty())
                    flank = away;
            }
        const int d = dist(me.position(), safe_pos(*target, me.position()));
        if (d >= 2 && d <= 3)
            for (const auto t : flank.positions())
                if (cast_at(dash, t))
                    break;
        if (dist(me.position(), safe_pos(*target, me.position())) > 1)
            step_towards(flank.is_empty() ? ring(safe_pos(*target, me.position()), 1, 1) : flank);
        attack_with_reset(*target, reset);
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        team_farm(ctx, abilities, farm_target);
        return;
    }
    // Farming: lead her squad to the chosen camp (choose_objective already ran).
    if (!farm_target) {
        if (ctx.objective)
            step_towards(ring(ctx.objective->tile, 2, 4));
        return;
    }
    farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Tavi

void Bot::marksman_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, is_single);
    const auto* reset = find_ability(abilities, is_reset);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    (void)nearest;
    const bool engage = !ctx.enemies.empty() && fighting_back();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        const auto target = top_threat(ctx.enemies);
        // Damage + stun, then a basic attack; the reset waits for a later turn
        // because only one ability can be cast per turn.
        if (target)
            cast_on(stun, *target);
        if (attack_enemies(ctx.enemies, reset))
            return;
        if (target)
            keep_distance(safe_pos(*target, me.position()), 3, 3);
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        team_farm(ctx, abilities, farm_target);
        return;
    }
    // Farming: share cleared camps, shoot what Ryth has woken, stay just behind him.
    if (join_capture())
        return;
    if (farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, ctx.anchor))
        return;
    if (ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        step_towards(ring(*ctx.anchor, 1, 2));
        return;
    }
    if (farm_target) // Ryth is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Willow

void Bot::support_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* heal = find_ability(abilities, is_heal);
    const auto* reset = find_ability(abilities, is_reset);

    // Heal the ally below 30% in the greatest danger (most enemies near),
    // breaking ties by lowest health percentage.
    std::optional<cambc::champion> patient;
    int patient_danger = -1;
    for (const auto& ally : ctx.allies) {
        if (health_pct(ally) >= 0.3f)
            continue;
        int danger = 0;
        for (const auto& e : ctx.enemies)
            danger += alive(e) && dist(safe_pos(e, vec2{-1000, -1000}), ally.position()) <= 4;
        if (danger > patient_danger
            || (danger == patient_danger && health_pct(ally) < health_pct(*patient))) {
            patient = ally;
            patient_danger = danger;
        }
    }
    bool healed = false;
    if (patient) {
        healed = cast_on(heal, *patient);
        if (!healed && dist(me.position(), safe_pos(*patient, me.position())) > 4)
            step_towards(square(safe_pos(*patient, me.position()), 4) & ~square(safe_pos(*patient, me.position()), 1));
    }

    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    (void)nearest;
    const bool engage = !ctx.enemies.empty() && fighting_back();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }
    const auto* attack_reset = healed ? nullptr : reset;
    if (engage) {
        if (attack_enemies(ctx.enemies, attack_reset))
            return;
        // Back line: near Ryth (or the team) but at least 4 tiles from enemies.
        const vec2 centre = ctx.anchor ? *ctx.anchor
            : (ctx.allies.empty() ? me.position() : ctx.allies.front().position());
        bitboard safe = square(centre, 4);
        for (const auto& e : ctx.enemies)
            safe &= ~square(e.position(), 3);
        if (!safe.is_empty())
            step_towards(safe);
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        team_farm(ctx, abilities, farm_target);
        return;
    }
    // Farming: share cleared camps, help on woken camps, back line behind Ryth.
    if (join_capture())
        return;
    if (farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, ctx.anchor))
        return;
    if (ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        step_towards(ring(*ctx.anchor, 2, 3));
        return;
    }
    if (farm_target) // Ryth is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}

// ---------------------------------------------------------------- Prism

void Bot::mage_turn() {
    auto& me = cambc::self();
    const auto ctx = read_context(squad_a);
    const auto abilities = me.abilities();
    const auto* stun = find_ability(abilities, is_single);
    const auto* area = find_ability(abilities, is_area);
    const int nearest = nearest_enemy_distance(ctx.enemies, me.position());
    // On 5-champion maps Prism opens the fight when hunting.
    (void)nearest;
    const bool engage = !ctx.enemies.empty() && fighting_back();
    if (should_retreat(ctx)) {
        retreat(ctx);
        return;
    }

    if (engage) {
        bool cast = false;
        // Finisher: area spell on a low enemy, but only if it kills or hits 2+.
        for (const auto& low : ctx.enemies) {
            if (cast || health_pct(low) >= 0.3f || !ready(area))
                continue;
            const auto [tile, hits] = best_area_tile(area, ctx.enemies, low.position());
            const bool kills = attempt([&] { return area->damage_amount(low) >= low.health(); });
            if (tile && (hits >= 2 || kills))
                cast = cast_at(area, *tile);
        }
        // Opener: damage + stun on the greatest threat in range.
        if (!cast)
            if (const auto target = top_threat(ctx.enemies, 4)) {
                cast = cast_on(stun, *target);
                if (cast)
                    team_memory::raise_fight();
            }
        // Follow-up: area spell once the stun is on cooldown.
        if (!cast) {
            const auto [tile, hits] = best_area_tile(area, ctx.enemies);
            if (tile && hits >= 1)
                cast_at(area, *tile);
        }
        if (attack_enemies(ctx.enemies, nullptr))
            return;
        if (const auto target = top_threat(ctx.enemies))
            keep_distance(safe_pos(*target, me.position()), 2, 4);
        return;
    }

    if (game_phase() == Phase::early && !corners_map_active) {
        team_farm(ctx, abilities, farm_target);
        return;
    }
    if (cambc::map::width() == 80 && cambc::map::height() == 80
        && me.champion_name() == cambc::champion_type::cassi && farm_target) {
        farm_group(*farm_target, abilities);
        return;
    }
    // Farming: share cleared camps, hit camps the squad leader has woken, and
    // stay a step behind the leader so the camp targets the leader first.
    if (join_capture())
        return;
    // Early on Stellar stays with Ryth on camps that pay gold (gem clusters pay none).
    const bool feeding = me.champion_name() == cambc::champion_type::stellar && game_phase() == Phase::early
        && squad_a && ctx.anchor;
    if (!feeding && farm_nearby_cluster(abilities, solo_cluster_radius))
        return;
    if (farm_nearby_wolf(abilities, squad_a ? ctx.anchor
                                            : (ctx.objective ? std::optional<vec2>(ctx.objective->tile) : std::nullopt)))
        return;
    if (squad_a && ctx.anchor) {
        if (hit_camps(std::nullopt, abilities))
            return;
        step_towards(ring(*ctx.anchor, 1, 2));
        return;
    }
    if (!squad_a && ctx.objective) {
        if (hit_camps(std::nullopt, abilities))
            return;
        step_towards(ring(ctx.objective->tile, ctx.hunt ? 3 : 2, ctx.hunt ? 5 : 3));
        return;
    }
    if (farm_target) // the squad leader is dead or recalling: farm alone
        farm_group(*farm_target, abilities);
}
