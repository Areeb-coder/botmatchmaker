// Map-aware draft. Both teams pick simultaneously and may share champions, so pick order only
// matters for information: we greedily build the best set for this map, re-scored every round
// against what we actually hold (a missed turn gets a random pick that the next round adapts to).
#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include "plan.h"

using cambc::champion_draft;
using cambc::champion_type;

namespace {

constexpr int champion_count = 12;

// Static kit facts from GameRules.md. `base` is ~sustained damage per turn / 75 plus kit value,
// so an average pick is about 1.0 before map adjustments.
struct traits {
    const char* name;
    float base;
    float health;
    bool melee;
    bool tank;   // melee with >= 900 health and a reason to stand in front
    bool stun;
    bool multi;  // hits several targets: radius-1 AoE spell or splash basic attacks
};

constexpr std::array<traits, champion_count> roster{{
    /* ryth */            {"ryth", 1.00f, 1200, true, true, true, true},
    /* vesper_vale */     {"vesper_vale", 0.70f, 900, false, false, false, false},
    /* vendetta */        {"vendetta", 1.00f, 800, true, false, false, false},
    /* cassi_and_issac */ {"cassi_and_issac", 1.25f, 900, true, false, true, false},
    /* stellar */         {"stellar", 1.10f, 750, false, false, false, true},
    /* prism */           {"prism", 0.75f, 700, false, false, true, true},
    /* marcella */        {"marcella", 0.85f, 1200, true, true, true, false},
    /* willow */          {"willow", 0.60f, 700, false, false, false, false},
    /* ren */             {"ren", 0.80f, 900, true, true, true, false},
    /* varg */            {"varg", 0.80f, 1000, true, true, false, true},
    /* tavi */            {"tavi", 0.90f, 750, false, false, true, false},
    /* heart_of_ruin */   {"heart_of_ruin", 1.10f, 1000, true, true, true, true},  // v12 (B25): self-shield, stunning area dash, +50% damage at low health
}};

// Safe order if the map profile cannot be computed (or fuel runs short).
constexpr std::array fallback_order{
    champion_draft::cassi_and_issac, champion_draft::stellar, champion_draft::ryth, champion_draft::vendetta,
    champion_draft::tavi, champion_draft::varg, champion_draft::marcella, champion_draft::ren,
    champion_draft::prism, champion_draft::vesper_vale, champion_draft::willow, champion_draft::heart_of_ruin,
};

// Below this much fuel, skip analysis and pick from the fallback order. Missing a pick randomises
// every remaining pick. The profile (~210k on an 80x80 map) only fits the first turn's 10x budget;
// scoring a cached profile costs a few thousand.
constexpr std::uint64_t min_fuel_for_profile = 500'000;
constexpr std::uint64_t min_fuel_for_scoring = 40'000;

const traits& info(champion_draft pick) { return roster[static_cast<int>(pick)]; }

float clamp01(float x) { return std::clamp(x, 0.f, 1.f); }

bool contains(const std::vector<champion_draft>& picks, champion_draft pick) {
    return std::find(picks.begin(), picks.end(), pick) != picks.end();
}

// Map signals, each 0..1.
struct signals {
    int picks;
    float free, boss, elk, splash, aoe, contested;
    float gold_bonus;  // gold per gem above 3: elkring's elk + gold camps
    float big;         // map area per pick: travel-bound maps like expanse
    float brawl;       // spawns close together: constant fighting
};

signals read_signals(const map_profile& p) {
    return {
        .picks = p.team_size,
        .free = p.share(p.free_gems),
        .boss = p.share(p.boss_gems),
        .elk = p.share(p.elk_gems),
        .splash = p.splash_share,
        .aoe = p.aoe_share,
        .contested = p.contested_share,
        .gold_bonus = std::max(0.f, p.gold_per_gem() - 3.f),
        .big = clamp01((p.area_per_pick() - 600.f) / 1200.f),
        .brawl = clamp01((45.f - p.spawn_distance) / 20.f),
    };
}

// What the map adds to a champion, independent of our other picks.
float map_bonus(champion_draft pick, const signals& s) {
    switch (pick) {
    case champion_draft::cassi_and_issac:
        // A fourth body on 3-pick maps; more bodies cover big maps. Linked death hurts in crowds.
        return (s.picks == 3 ? 0.35f : 0.f) + 0.2f * s.big - (s.picks >= 5 ? 0.2f * s.brawl : 0.f);
    case champion_draft::stellar: return 0.35f * s.aoe + 0.15f * s.contested;
    case champion_draft::vendetta: return 0.2f * s.elk + 0.1f * s.big;  // kill resets on lone elks, dash
    case champion_draft::ryth: return 0.2f * s.brawl + 0.2f * s.contested + 0.25f * s.boss + 0.1f * s.elk;
    case champion_draft::tavi: return 0.4f * s.big + 0.2f * s.boss;  // shops in the field
    case champion_draft::marcella: return 0.15f * s.elk + 0.15f * s.brawl;
    case champion_draft::varg: return 0.6f * s.splash;
    case champion_draft::ren: return 0.15f * s.elk + 0.2f * s.boss;
    case champion_draft::prism: return 0.2f * s.aoe + 0.2f * s.boss;
    case champion_draft::vesper_vale: return 0.3f * s.gold_bonus;
    case champion_draft::willow: return 0.2f * (s.picks - 3) + 0.25f * s.boss + 0.15f * s.contested;
    case champion_draft::heart_of_ruin: return 0.2f * s.brawl + 0.15f * s.contested;
    }
    return 0.f;
}

// What the rest of our team adds or takes away.
float composition_bonus(champion_draft pick, const signals& s, const std::vector<champion_draft>& ours) {
    int tanks = 0, stuns = 0, multis = 0;
    for (auto held : ours) {
        tanks += info(held).tank;
        stuns += info(held).stun;
        multis += info(held).multi;
    }
    const auto& t = info(pick);
    float bonus = 0;
    if (t.tank) bonus += tanks == 0 ? 0.2f + 0.2f * (s.elk + s.boss) : tanks >= 2 ? -0.2f : 0.f;
    if (t.stun && stuns < 3) bonus += 0.3f * s.boss;  // chain stuns to keep a monolith quiet
    if (t.multi && multis >= 2) bonus -= 0.15f;
    return bonus;
}

// Champion-vs-champion strength for choosing the gem duo: damage, staying power and stuns.
float fight_score(champion_draft pick) {
    const auto& t = info(pick);
    return t.base + t.health / 3000.f + (t.stun ? 0.15f : 0.f);
}

champion_type body_type(champion_draft pick) {
    // champion_type splits cassi_and_issac into cassi and issac; later entries shift by one.
    const int index = static_cast<int>(pick);
    return static_cast<champion_type>(index <= static_cast<int>(champion_draft::cassi_and_issac) ? index : index + 1);
}

// Greedily extend our picks to a full team; the first new entry is this round's pick.
// v12 (B25): the top 20's core, drafted in this order (pick rates over 750 games: Vesper 90%, Heart of Ruin 84%,
// Marcella 82%, Vendetta 67%, Ryth 45%); map scoring fills the rest.
constexpr std::array core_order{champion_draft::vesper_vale, champion_draft::heart_of_ruin, champion_draft::marcella,
                                champion_draft::vendetta, champion_draft::ryth};

std::vector<champion_draft> project_team(const map_profile& profile, std::vector<champion_draft> team) {
    for (auto pick : core_order)
        if (static_cast<int>(team.size()) < profile.team_size && !contains(team, pick))
            team.push_back(pick);
    while (static_cast<int>(team.size()) < profile.team_size) {
        const auto scores = score_champions(profile, team);
        const auto best = std::max_element(scores.begin(), scores.end()) - scores.begin();
        team.push_back(static_cast<champion_draft>(best));
    }
    return team;
}

std::vector<champion_draft> fallback_team(std::vector<champion_draft> team, std::size_t size) {
    for (auto pick : fallback_order)
        if (team.size() < size && !contains(team, pick))
            team.push_back(pick);
    return team;
}

draft_plan make_plan(const std::vector<champion_draft>& team, const std::optional<map_profile>& profile) {
    draft_plan plan;
    plan.team_size = static_cast<std::uint8_t>(team.size());
    for (std::size_t i = 0; i < team.size() && i < std::size(plan.picks); ++i)
        plan.picks[i] = static_cast<std::uint8_t>(team[i]);

    // Gem duo: Cassi & Issac when we have them (two bodies for one pick, and their shared death
    // makes keeping them together free); otherwise the two strongest fighters. The rest farm gold.
    for (auto pick : team) {
        const auto body = static_cast<int>(body_type(pick));
        plan.roles[body] = role::gold_squad;
        if (pick == champion_draft::cassi_and_issac)
            plan.roles[static_cast<int>(champion_type::issac)] = role::gold_squad;
    }
    if (contains(team, champion_draft::cassi_and_issac)) {
        plan.roles[static_cast<int>(champion_type::cassi)] = role::gem_duo;
        plan.roles[static_cast<int>(champion_type::issac)] = role::gem_duo;
    } else {
        auto fighters = team;
        std::sort(fighters.begin(), fighters.end(),
                  [](auto a, auto b) { return fight_score(a) > fight_score(b); });
        for (std::size_t i = 0; i < fighters.size() && i < 2; ++i)
            plan.roles[static_cast<int>(body_type(fighters[i]))] = role::gem_duo;
    }

    if (profile) {
        plan.features = profile->to_features();
        plan.split_farm = read_signals(*profile).big >= 0.5f;
    }
    return plan;
}

void log_analysis(const map_profile& p, const std::vector<champion_draft>& team) {
    const auto s = read_signals(p);
    std::cout << std::fixed << std::setprecision(2)
              << "draft: map " << p.width << 'x' << p.height << " picks=" << p.team_size << " altars=" << p.altars
              << " gems=" << p.total_gems << " gold=" << p.total_gold << " spawn_dist=" << p.spawn_distance
              << " camp_dist=" << p.camp_distance << '\n'
              << "draft: signals free=" << s.free << " boss=" << s.boss << " elk=" << s.elk << " splash=" << s.splash
              << " aoe=" << s.aoe << " contested=" << s.contested << " gold_bonus=" << s.gold_bonus
              << " big=" << s.big << " brawl=" << s.brawl << '\n';
    const auto scores = score_champions(p, {});
    std::cout << "draft: opening scores";
    for (int i = 0; i < champion_count; ++i)
        std::cout << ' ' << roster[i].name << '=' << scores[i];
    std::cout << "\ndraft: projected team";
    for (auto pick : team)
        std::cout << ' ' << info(pick).name;
    std::cout << std::endl;
}

} // namespace

std::array<float, champion_count> score_champions(const map_profile& profile, const std::vector<champion_draft>& ours) {
    const auto s = read_signals(profile);
    std::array<float, champion_count> scores{};
    for (int i = 0; i < champion_count; ++i) {
        const auto pick = static_cast<champion_draft>(i);
        scores[i] = contains(ours, pick) ? -std::numeric_limits<float>::infinity()
                                         : info(pick).base + map_bonus(pick, s) + composition_bonus(pick, s, ours);
    }
    return scores;
}

// Experiments (tools/arena.py seed_fuel) run with a fuel quantum of 1,000,000 + 10,000 * seed, far above
// the real 100k, and get a random roster so a deterministic engine still yields many distinct games.
// Returns 0 (play for real) below 1M. The first draft turn gets 10x fuel.
// v11 (ported back for testing; inert without the header): tools/arena.py writes experiment_seed.h into a copy of the bot to pick the seed at compile time, so seeded
// games run at the real 100k fuel (opponents' fuel limits then behave as in ranked). Without the header the fuel
// quantum decides, as before.
#if __has_include("experiment_seed.h")
#include "experiment_seed.h"
#endif

static int experiment_seed() {
#ifdef EXPERIMENT_SEED
    return EXPERIMENT_SEED;
#endif
    const bool first_turn = cambc::draft::current_draft(cambc::team::ally).empty();
    const double quantum = static_cast<double>(cambc::fuel_remaining()) / (first_turn ? 10 : 1);
    return quantum < 995'000 ? 0 : static_cast<int>((quantum - 1'000'000) / 10'000 + 0.5);
}

static std::vector<champion_draft> random_team(int seed, std::vector<champion_draft> team, std::size_t size) {
    const auto spawn = cambc::map::spawn_region(cambc::team::ally).positions()[0];
    std::uint32_t h = static_cast<std::uint32_t>(seed) * 2654435761u ^ spawn.x * 73856093u ^ spawn.y * 19349663u;
    while (team.size() < size) {
        h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
        const auto pick = static_cast<champion_draft>(h % champion_count);
        if (!contains(team, pick)) team.push_back(pick);
    }
    return team;
}

void play_draft() {
    static const int seed = experiment_seed();  // read before anything spends fuel on the first turn
    static std::optional<map_profile> profile;
    static bool profile_failed = false, logged = false;

    const auto ours = cambc::draft::current_draft(cambc::team::ally);
    const auto size = cambc::draft::team_size();
    if (ours.size() >= size)
        return;

    if (!profile && !profile_failed && cambc::fuel_remaining() > min_fuel_for_profile) {
        try {
            const auto before = cambc::fuel_remaining();
            profile = compute_map_profile();
            std::cout << "draft: profile fuel=" << before - cambc::fuel_remaining() << '\n';
        } catch (const std::exception& error) {
            profile_failed = true;
            std::cout << "draft: map profile failed: " << error.what() << std::endl;
        }
    }

    const bool can_score = profile && cambc::fuel_remaining() > min_fuel_for_scoring;
    const auto team = seed > 0 ? random_team(seed, ours, size)
        : can_score ? project_team(*profile, ours) : fallback_team(ours, size);
    // iostream float formatting costs ~100k fuel: only log with the first turn's 10x budget to spare.
    if (can_score && !logged && cambc::fuel_remaining() > 300'000) {
        log_analysis(*profile, team);
        logged = true;
    }

    // Plan first: if the pick throws we still leave the team a usable plan.
    const auto plan = make_plan(team, profile);
    cambc::draft_memory = std::span<const draft_plan, 1>{&plan, 1};
    cambc::draft::pick(team[ours.size()]);
    std::cout << "draft: round " << ours.size() << " picked " << info(team[ours.size()]).name
              << " fuel_left=" << cambc::fuel_remaining() << std::endl;
}
