#pragma once
// Map features the draft scores champions against. Computed once, during the draft.
#include "plan.h"

struct map_profile {
    int team_size = 0;
    int width = 0, height = 0;
    int altars = 0;

    // Gem totals for one full cycle of every camp group.
    int total_gems = 0, total_gold = 0;
    int free_gems = 0;  // gem/gold-cluster-only groups: no damage taken
    int boss_gems = 0;  // groups containing a monolith
    int elk_gems = 0;   // groups containing an elk (and no monolith)

    // Shares of non-monolith camp value (gems + gold / 4) that multi-target damage can reach.
    float splash_share = 0;  // another camp within Chebyshev 1: Varg splash
    float aoe_share = 0;     // another camp within Chebyshev 2: radius-1 ground AoE between them

    float contested_share = 0;  // gem share whose walk from each spawn differs by <= 25%
    int spawn_distance = 0;     // walk from our spawn to theirs
    float camp_distance = 0;    // gem-weighted mean walk from our spawn to each group

    // Derived 0..1 signals used by the scoring.
    float share(int gems) const { return total_gems ? float(gems) / total_gems : 0.f; }
    float gold_per_gem() const { return total_gems ? float(total_gold) / total_gems : 0.f; }
    // Map area per body we field (Cassi & Issac aside); expanse is ~2100, elkring ~180.
    float area_per_pick() const { return float(width * height) / team_size; }

    plan_features to_features() const;
};

map_profile compute_map_profile();
