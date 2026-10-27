#pragma once
#include <cambc.h>
#include <array>
#include <vector>
#include "map_profile.h"

// Pick one champion this draft round and refresh the plan in draft memory.
void play_draft();

// Score every champion for our next pick given the map and our picks so far; higher is better.
// Exposed for logging and tuning. Picks already taken by us score -infinity.
std::array<float, 12> score_champions(const map_profile& profile, const std::vector<cambc::champion_draft>& ours);
