#pragma once
// Draft plan: written to draft memory by the draft bot, read by every champion.
#include <cambc.h>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

enum class role : std::uint8_t {
    none = 0,    // not on our team
    gem_duo,     // greedily captures gem camps and fights for them; tributes everything
    gold_squad,  // farms gold: monsters first, then gold clusters
};

// Map features in 0..255 shares (255 = 100%) so the plan fits in plain bytes.
struct plan_features {
    std::uint8_t free_share;       // gems in groups with no attacking monster
    std::uint8_t boss_share;       // gems in groups containing a monolith
    std::uint8_t elk_share;        // gems in elk groups (no monolith)
    std::uint8_t splash_share;     // camp value with another camp within radius 1 (Varg splash)
    std::uint8_t aoe_share;        // camp value with another camp within radius 2 (radius-1 ground AoE)
    std::uint8_t contested_share;  // gems roughly equidistant from both spawns
    std::uint16_t total_gems;      // one full cycle of every camp group
    std::uint16_t total_gold;
    std::uint16_t spawn_distance;  // walking distance between the two spawns
    std::uint16_t camp_distance;   // gem-weighted mean walking distance from our spawn to groups
};

struct draft_plan {
    static constexpr std::uint8_t magic0 = 'C', magic1 = 'B', current_version = 2;
    static constexpr std::uint8_t no_pick = 0xFF;

    std::uint8_t m0 = magic0, m1 = magic1, version = current_version;
    std::uint8_t team_size = 0;
    std::uint8_t picks[5] = {no_pick, no_pick, no_pick, no_pick, no_pick};  // champion_draft, in pick order
    role roles[13] = {};  // indexed by champion_type
    std::uint8_t split_farm = 0;  // 1 = large map per body: farm separately rather than as a pack
    plan_features features = {};

    role role_of(cambc::champion_type type) const { return roles[static_cast<int>(type)]; }
};
static_assert(std::is_trivially_copyable_v<draft_plan>);
static_assert(sizeof(draft_plan) <= 1024);

// Snapshot of draft memory; nullopt if the draft never wrote a valid plan.
inline std::optional<draft_plan> read_plan() {
    const auto& memory = cambc::draft_memory;
    if (memory.size() < sizeof(draft_plan))
        return std::nullopt;
    draft_plan plan;
    std::memcpy(&plan, memory.data(), sizeof plan);
    if (plan.m0 != draft_plan::magic0 || plan.m1 != draft_plan::magic1 || plan.version != draft_plan::current_version)
        return std::nullopt;
    return plan;
}
