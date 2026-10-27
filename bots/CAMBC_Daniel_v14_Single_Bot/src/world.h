#pragma once
// What this champion knows about camp groups. Vision is per champion and engine queries cost
// ~150 fuel each (100k per turn), so static group data is cached once and only groups in
// vision are re-queried.
#include <cambc.h>
#include <cstdint>
#include <vector>

// Attack range of a camp kind (week 2 patch: elk and golem 4, monolith 5).
int camp_range(cambc::camp_type kind);

struct group_info {
    cambc::map::camp_group group;
    int gems = 0, gold = 0;
    bool monolith = false;
    bool mobs = false;      // has a wolf or elk: costs health and pays gems with its gold
    bool gold_only = false; // only gold clusters: "mining"
    float health = 0;       // total health of every member
    float damage = 0;       // damage per turn the awake group deals
    // Damage taken killing the members one at a time, times our damage per turn: divide by our
    // damage per turn for the expected health cost. Each member hits until it dies.
    float exposure = 0;
    cambc::vec2 anchor;     // first spawn tile
    cambc::bitboard tiles;  // spawn tiles; standing on one captures
    // Walking distances from the anchor, filled in by the bot: to our spawn (trips home to heal)
    // and to the nearest tile an altar accepts gems from (the tribute trip after a capture).
    int home_distance = 0, altar_distance = 0;

    // Knowledge from this champion's own observations.
    int respawn_round = 0;   // known respawning until this round
    bool reward_ready = false;  // all members dead, reward waiting, as of last sight
    int seen_round = -1;
    int skip_until = 0;      // gave up waiting for help here; ignore until this round
    // Monolith groups: health left on living members as of the last full sight. Camps never heal
    // (measured: a monolith sat at 9,205 for 260 rounds), so siege damage carries over between trips.
    float remaining = 0;
    bool siege_target = false;  // v7: chosen as a whole-team siege (set by choose_target)

    bool known_respawning(int round) const { return respawn_round > round; }
};

class world {
public:
    void init();
    // Refresh knowledge of every group whose spawn tiles are in vision.
    void observe(int round);

    std::vector<group_info>& groups() { return groups_; }
    // The camp that spawns at `p`, from the cache. Camps never leave their spawn tile. Use this,
    // not vision::camp_at, which reloads every camp group from the engine (~30k fuel per call).
    const cambc::camp* camp_at(cambc::vec2 p) const;
    group_info* group(int index) { return index >= 0 && index < int(groups_.size()) ? &groups_[index] : nullptr; }
    bool ready() const { return !groups_.empty(); }

private:
    std::vector<group_info> groups_;
    std::vector<std::int16_t> group_at_;  // row-major tile -> group index, -1 for none
    cambc::bitboard all_tiles_;
    int width_ = 0;
};
