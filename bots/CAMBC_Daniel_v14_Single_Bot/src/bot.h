#pragma once
#include <cambc.h>
#include <optional>
#include "plan.h"
#include "world.h"

// Our own figures for target estimates, measured once per turn.
struct self_estimate {
    float damage = 50, health = 900;
};

// Per-champion state survives between turns.
// Strategy v4 (see bots/docs/v4): every body farms its own best group and claims it in shared
// memory; bodies share a group when it needs them or beats their other options. Everyone tributes
// the gems they carry.
class Bot {
    cambc::distance_field progress_field;  // walking distance to the progress target (v7 stall check)
    int progress_field_target = -1;
    float last_health = 0;
    int last_health_round = -10, hidden_round = -10;  // v7 unseen-attacker detection
    std::optional<draft_plan> plan;  // from draft memory; nullopt if the draft wrote none
    role squad = role::gold_squad;   // the draft's role: now only steers fighting and shopping
    int slot = 0;                    // our claim slot in shared memory
    int team_bodies = 3;             // bodies on our team (Cassi and Issac count as two)
    world camps;
    self_estimate own;
    bool initialised = false;
    bool moved = false;              // this turn: blocks casts that exclude walking
    int waiting_for = -1, waited = 0;  // group we are holding off from until help arrives
    int current_target = -1, target_round = -1;
    int last_enemy_round = -1;       // last round an enemy champion was in sight (v6 threat banking)  // chosen group, and the round it was chosen
    int claim = -1;                  // group we hold this turn, published at its end
    int published_claim = -2, published_round = -1000;
    bool banking = false;            // committed to tributing what we carry
    cambc::distance_field altar_walk;  // walking distance to the nearest tribute tile
    // v11 (B19): choose_target's walking field from our position, reused until we move 3+ tiles or
    // 10 rounds pass. A fresh field each turn cost ~40k fuel on 01big_world (100x100) and froze the bot.
    cambc::distance_field target_field;
    cambc::vec2 target_field_origin{-100, -100};
    int target_field_round = -100;
    bool altar_walled = false;       // v13 (B29): every walk to an altar crosses a camp (02the-pack)
    cambc::bitboard corridor, corridor_beside, corridor_gather;  // v13 (B29): camp tiles to cut to reach it; within 1 and 3
    bool gems_deliverable = true;    // v8: some altar has a free tile next to it (not 01boss-battle)

    float gem_value(const group_info& g) const { return gems_deliverable ? static_cast<float>(g.gems) : 0.f; }
    // Turns to walk out of a Monolith's range from where we attack it.
    int monolith_exit_turns() const;
    // Progress watchdog on the current target (see watch_progress).
    int progress_target = -1, progress_round = -1, best_distance = 0, stalled = 0;
    bool hit_target = false;

    void init();
    void take_turn(int round);
    void publish_claim(int round);
    void watch_progress(int round);
    void shop();
    bool place_lock();
    bool fight_enemies(int round);
    bool should_tribute(int round);
    void go_tribute();
    bool shop_trip();
    int choose_target(int round);
    void work_group(int index, int round);
    void retreat();

    // Movement and combat helpers that track `moved`.
    bool move_towards_closest(cambc::bitboard goals);
    void breach();
    template <class Target> bool strike(const Target& target);
    template <class Target> void cast_offense(const Target& target);
    void cast_support();

public:
    void play_turn();
};
