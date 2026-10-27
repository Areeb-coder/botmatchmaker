#include <cambc.h>
#include "draft.h"
#include <algorithm>
#include <array>

void play_draft() {
    // Draft for the contested Monolith maps and Collision's contested wolf race.
    using cambc::champion_draft;
    const std::array standard_lineup{champion_draft::cassi_and_issac, champion_draft::stellar,
        champion_draft::ren, champion_draft::varg, champion_draft::ryth};
    const std::array contested_camp_lineup{champion_draft::prism, champion_draft::stellar,
        champion_draft::cassi_and_issac, champion_draft::ryth, champion_draft::tavi};
    const std::array contest_meta_lineup{champion_draft::cassi_and_issac, champion_draft::stellar,
        champion_draft::ren, champion_draft::varg, champion_draft::ryth};
    // Mirror the winning rank-5 draft on the five maps where its lineup was
    // exposed in the latest official group.
    const std::array contest_top5_lineup{champion_draft::varg, champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::stellar, champion_draft::tavi};
    // On four-pick Interlock, take Tavi before the opponent can claim its
    // ranged carry; keep the C&I pair, Ryth, and Stellar for the rest of the team.
    const std::array interlock_tavi_lineup{champion_draft::tavi, champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::stellar, champion_draft::varg};
    // Use one independent fighter and Willow support instead of the linked
    // Cassi/Issac pair, keeping the tank, mage, and marksman.
    const std::array contest_willow_independent{champion_draft::varg, champion_draft::ryth,
        champion_draft::stellar, champion_draft::tavi, champion_draft::willow};
    // Keep Willow while adding C&I's second body; give up Varg on Contest.
    const std::array contest_willow_six_body{champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::stellar, champion_draft::tavi,
        champion_draft::willow};
    const std::array collision_lineup{champion_draft::stellar, champion_draft::cassi_and_issac,
        champion_draft::varg, champion_draft::ryth, champion_draft::tavi};
    const std::array expanse_lineup{champion_draft::cassi_and_issac, champion_draft::stellar,
        champion_draft::varg, champion_draft::ren, champion_draft::ryth};
    const std::array rows_lineup{champion_draft::varg, champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::stellar, champion_draft::ren};
    const std::array lanes_lineup{champion_draft::varg, champion_draft::cassi_and_issac,
        champion_draft::ryth, champion_draft::stellar, champion_draft::tavi};
    // Elkring keeps the standard five champions but puts Varg before Ryth.
    const std::array elkring_lineup{champion_draft::stellar, champion_draft::cassi_and_issac,
        champion_draft::varg, champion_draft::ryth, champion_draft::tavi};
    // Mirror the compact Varg / Stellar / Ryth core seen from the top-5
    // opponent on Fragment; Elkring keeps its separate five-pick ordering.
    const std::array fragment_lineup{champion_draft::varg, champion_draft::stellar,
        champion_draft::ryth, champion_draft::cassi_and_issac, champion_draft::ren};
    const auto& altars = cambc::map::altars();
    const bool contest = cambc::map::width() == 50 && cambc::map::height() == 50
        && altars.is_set(cambc::vec2{25, 24}) && altars.is_set(cambc::vec2{24, 25});
    const bool stampede = cambc::map::width() == 60 && cambc::map::height() == 50
        && altars.is_set(cambc::vec2{31, 23}) && altars.is_set(cambc::vec2{31, 26});
    const bool collision = cambc::map::width() == 41 && cambc::map::height() == 41
        && altars.is_set(cambc::vec2{27, 20}) && altars.is_set(cambc::vec2{39, 20});
    const bool expanse = cambc::map::width() == 80 && cambc::map::height() == 80;
    const bool rushbait = cambc::map::width() == 30 && cambc::map::height() == 80;
    const bool eight = cambc::map::width() == 41 && cambc::map::height() == 30;
    const bool interlock = cambc::map::width() == 40 && cambc::map::height() == 40;
    const bool rows = cambc::map::width() == 50 && cambc::map::height() == 50
        && altars.is_set(cambc::vec2{15, 16});
    const bool lanes = cambc::map::width() == 90 && cambc::map::height() == 30;
    const bool elkring = cambc::map::width() == 30 && cambc::map::height() == 30
        && altars.is_set(cambc::vec2{26, 3}) && altars.is_set(cambc::vec2{3, 26});
    const bool fragment = cambc::map::width() == 30 && cambc::map::height() == 30 && !elkring;
    const auto& lineup = elkring ? elkring_lineup
        : fragment ? fragment_lineup
        : contest ? contested_camp_lineup
        : interlock ? interlock_tavi_lineup
        : (rushbait || eight) ? contest_top5_lineup
        : stampede ? contested_camp_lineup
        : collision ? collision_lineup
        : lanes ? lanes_lineup
        : expanse ? expanse_lineup
        : rows ? rows_lineup : standard_lineup;

    // Fish's strongest observed drafts use Ren with Stellar and Cassi/Issac.
    // If that pick is visible, counter by adding Ren; on Rows the three-pick
    // signature is Cassi/Issac + Stellar + Varg before Fish's final Ren pick.
    const std::array rushbait_ren_counter{champion_draft::ren, champion_draft::cassi_and_issac,
        champion_draft::stellar, champion_draft::ryth, champion_draft::varg};
    const std::array rows_ren_counter{champion_draft::ren, champion_draft::cassi_and_issac,
        champion_draft::stellar, champion_draft::varg, champion_draft::ryth};
    const std::array five_pick_ren_counter{champion_draft::ren, champion_draft::stellar,
        champion_draft::cassi_and_issac, champion_draft::ryth, champion_draft::varg};
    const std::array swirl_vendetta_counter{champion_draft::cassi_and_issac,
        champion_draft::stellar, champion_draft::vendetta, champion_draft::varg,
        champion_draft::ren};
    // Side A keeps Ren's frontline but trades Varg's separate farm slot for
    // Vendetta when the opponent exposes the same C&I/Varg opening.
    const std::array swirl_side_a_vendetta_tank{champion_draft::cassi_and_issac,
        champion_draft::stellar, champion_draft::ren, champion_draft::vendetta,
        champion_draft::varg};
    const std::array interlock_ren_counter{champion_draft::ren, champion_draft::stellar,
        champion_draft::cassi_and_issac, champion_draft::ryth, champion_draft::tavi};
    const auto enemy = cambc::draft::current_draft(cambc::team::enemy);
    const auto has_enemy = [&](champion_draft pick) {
        return std::find(enemy.begin(), enemy.end(), pick) != enemy.end();
    };
    const bool enemy_ren = has_enemy(champion_draft::ren);
    const bool rows_ren_signature = rows && has_enemy(champion_draft::cassi_and_issac)
        && has_enemy(champion_draft::stellar) && has_enemy(champion_draft::varg)
        && !has_enemy(champion_draft::ryth);
    const bool swirl = cambc::map::width() == 45 && cambc::map::height() == 30;
    const auto& own_spawn = cambc::map::spawn_region(cambc::team::ally);
    const bool swirl_team_b = swirl && own_spawn.is_set(cambc::vec2{16, 21});
    // Vendetta is the side-B counter only when the opposing draft shows its
    // C&I/Varg opening; retain the normal roster against other Swirl drafts.
    const bool swirl_top_core = has_enemy(champion_draft::cassi_and_issac)
        && has_enemy(champion_draft::varg);
    // Side B trails the mirror on Eight with the ordinary Ren counter. Keep
    // Tavi there, and on either side against the observed top-team core.
    const auto& eight_spawn = cambc::map::spawn_region(cambc::team::ally);
    const auto spawn_distance = [&](cambc::vec2 corner) {
        int best = 1000;
        for (const auto tile : eight_spawn.positions())
            best = std::min(best, static_cast<int>(tile.chebyshev_distance(corner)));
        return best;
    };
    const bool eight_team_b = eight
        && spawn_distance({0, 0}) > spawn_distance({cambc::map::width() - 1, cambc::map::height() - 1});
    const bool eight_please_core = eight && enemy_ren
        && has_enemy(champion_draft::cassi_and_issac)
        && has_enemy(champion_draft::varg) && has_enemy(champion_draft::stellar);
    const bool eight_ren = eight && enemy_ren && (eight_team_b || eight_please_core);
    const auto& selected_lineup = swirl && swirl_top_core
        ? (swirl_team_b ? swirl_vendetta_counter : swirl_side_a_vendetta_tank)
        : eight_ren ? contest_top5_lineup
        : (enemy_ren || rows_ren_signature)
        ? rushbait ? rushbait_ren_counter
        : rows ? rows_ren_counter
        : interlock ? interlock_ren_counter
        : five_pick_ren_counter
        : lineup;

    const auto size = std::min<std::size_t>(cambc::draft::team_size(), selected_lineup.size());
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    if (chosen.size() >= size)
        return;
    for (std::size_t i = 0; i < size; ++i)
        if (std::find(chosen.begin(), chosen.end(), selected_lineup[i]) == chosen.end()) {
            cambc::draft::pick(selected_lineup[i]);
            return;
        }
}
