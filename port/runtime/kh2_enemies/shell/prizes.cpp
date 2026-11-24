// KH2's prizes (YS::PRIZEOBJ, YS::PRIZEEFFECT, the PS3 build's): what a script scatters, how it flies, lies and is
// taken.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// One size of a file's prizes (PRIZEEFFECT::PARAM): its kind in the file's effect (pppRyjMegaBirthPrize's: the sprite),
// how far under its place it stands on the ground (KH2's cm), its sounds as it bounces and as it is taken, and what it
// is worth
struct PrizeSize final {
    std::uint8_t kind;
    float height;
    int land, take;
    float worth;
};

// The morale orbs (PRIZE_MU: its PARAM and its gauge's values), the largest first, and the share of what is left to
// give its sizes take (PRIZE_MU::AppearGauge)
constexpr PrizeSize kOrbs[] = {{0, 40.3F, 41, 42, 2.0F}, {1, 27.9F, 41, 42, 0.5F}, {2, 18.6F, 41, 42, 0.1F}};
constexpr float kOrbShares[] = {0.9F, 0.9F, 1.0F};
constexpr const char *kOrbFile = "PRIZE_MU";

// Their ticks (KH2's 60 a second) and how many bounces are heard, how often the ground is asked for
constexpr float kPrizeLife = 900.0F, kPrizeFading = 12.0F, kTakenFor = 40.0F;
constexpr int kBouncesHeard = 2, kRecheck = 30;
constexpr float kPrizeRadius = 55.0F; // its collision's (PRIZEEFFECT::Init; KH2's cm)
constexpr float kPrizeGravity = 3.0F, kPrizeFall = 50.0F, kPrizeDrag = 0.95F;
constexpr float kPrizeBounce = -0.8F, kPrizeRest = 5.0F; // slower than this it lies
constexpr float kReachAcross = 120.0F, kReachUp = 30.0F;
constexpr float kSpiral = 30.0F, kSpiralMost = 0.59341F; // how far round it goes a tick, at most (34 degrees)

enum class Phase { moving, lying, taken };

struct Prize final {
    const PrizeSize *size = nullptr;
    Vec3 at{}, speed{}; // its place and its way a tick (KH2's cm)
    float life = kPrizeLife;
    int bounces = 0, recheck = kRecheck;
    bool ready = false; // once it falls: can be taken
    Phase phase = Phase::moving;
    unsigned taker = 0; // the Days player's object
    Vec3 way{};         // and the way from his middle to it as he took it, one long, and how far it was
    float far = 0.0F;
    std::uint8_t shown = 255, frame = 255; // its sprite's size (/ 255) and its frame (255 one at random to begin)
};

std::vector<Prize> prizes;
unsigned holder = 0; // the ally whose effects the prizes are, 0 none

float between(float from, float to)
{
    return from + (to - from) * float(kh2_chance()) * 2.32830644e-10F;
}

Enemy *prize_holder()
{
    const auto it = allies.find(holder);
    return holder != 0 && it != allies.end() && !it->second->released ? it->second.get() : nullptr;
}

// Moved a tick (PRIZEEFFECT::_phase_move): its way stopped and turned back by the room's walls on each axis, the ground
// met falling bounced off while it is fast, then lain on; the ground's pull
void move(Prize &p, const Enemy &e)
{
    const unsigned shell = shell_of(e);
    const float height = p.size->height;
    Vec3 foot = {p.at[0], p.at[1] + height, p.at[2]};
    Vec3 way = {p.speed[0], 0.0F, p.speed[2]};
    wall_sweep(shell, foot, kPrizeRadius, way, true);
    float from[3], to[3], hit[3];
    const Vec3 next = {foot[0] + way[0], foot[1] + p.speed[1], foot[2] + way[2]};
    to_days({next[0], foot[1] - kPrizeRadius, next[2]}, from);
    to_days({next[0], std::max(foot[1], next[1]) + 1.0F, next[2]}, to);
    const bool ground = p.speed[1] >= 0.0F && khdays_kh2_shell_ray(shell, from, to, hit) != 0;
    foot = next;
    if (ground && to_kh2(hit)[1] <= next[1]) {
        foot[1] = to_kh2(hit)[1];
        if (p.speed[1] > kPrizeRest) {
            p.speed[1] *= kPrizeBounce;
            if (p.bounces++ < kBouncesHeard)
                sound(e, p.size->land);
        } else {
            p.speed = {};
            p.phase = Phase::lying;
        }
    }
    if (std::fabs(way[0] - p.speed[0]) > 0.1F && p.phase == Phase::moving)
        p.speed[0] = -p.speed[0];
    if (std::fabs(way[2] - p.speed[2]) > 0.1F && p.phase == Phase::moving)
        p.speed[2] = -p.speed[2];
    p.at = {foot[0], foot[1] - height, foot[2]};
    p.speed[1] = std::min(p.speed[1] + kPrizeGravity, kPrizeFall);
    p.speed[0] *= kPrizeDrag;
    p.speed[2] *= kPrizeDrag;
    if (p.speed[1] >= 0.0F)
        p.ready = true;
}

// Whether it lies on the ground still, asked every half second (PRIZEEFFECT::phase_land)
void recheck(Prize &p, const Enemy &e)
{
    if (--p.recheck > 0)
        return;
    p.recheck = kRecheck;
    float from[3], to[3], hit[3];
    const float foot = p.at[1] + p.size->height;
    to_days({p.at[0], foot - kPrizeRadius, p.at[2]}, from);
    to_days({p.at[0], foot + 5.0F, p.at[2]}, to);
    if (!khdays_kh2_shell_ray(shell_of(e), from, to, hit))
        p.phase = Phase::moving;
}

// A Days player near enough (PRIZE::CanGet: 120 cm along the ground, 30 up or down of where it stands) takes it, its
// worth on the mission's gauge (PRIZE::CheckGetGauge)
void take(Prize &p, const Enemy &e)
{
    if (!p.ready)
        return;
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size()));
    for (int i = 0; i < n && i < int(list.size()); ++i) {
        const KhdaysKh2Player &player = list[std::size_t(i)];
        const auto actor = player_actors.find(player.object);
        if (actor != player_actors.end() && khdays_kh2_shell_player_hp(actor->second) <= 0)
            continue;
        const Vec3 at = to_kh2(player.place);
        if (std::fabs(at[1] - (p.at[1] + p.size->height)) > kReachUp ||
            std::hypot(at[0] - p.at[0], at[2] - p.at[2]) > kReachAcross)
            continue;
        Object &o = player_object(player.object);
        o.set_place(at);
        Vec3 middle = at;
        the_field().aim_point(o, middle);
        const Vec3 d = {p.at[0] - middle[0], p.at[1] - middle[1], p.at[2] - middle[2]};
        p.far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        p.way = p.far > 0.0F ? Vec3{d[0] / p.far, d[1] / p.far, d[2] / p.far} : Vec3{};
        p.taker = player.object;
        p.phase = Phase::taken;
        p.life = kTakenFor;
        sound(e, p.size->take);
        the_field().mission_gauge(9, 0, p.size->worth);
        if (tracing())
            khdays_log("kh2 enemies: player %08x takes an orb worth %.1f of the mission's gauge", player.object,
                       double(p.size->worth));
        return;
    }
}

// Round its taker's middle, nearer each tick, till it is in it (PRIZEEFFECT::phase_get); whether it goes on
bool spiral(Prize &p, const Enemy &e)
{
    p.life -= 1.0F;
    std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
    const int n = khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size()));
    const auto taker = std::find_if(list.begin(), list.begin() + std::min(n, int(list.size())),
                                    [&p](const KhdaysKh2Player &x) { return x.object == p.taker; });
    if (p.life <= 0.0F || taker == list.begin() + std::min(n, int(list.size())))
        return false;
    Object &o = player_object(p.taker);
    o.set_place(to_kh2(taker->place));
    Vec3 middle = o.place();
    the_field().aim_point(o, middle);
    const float near = p.far * p.life / kTakenFor, turn = std::min(kSpiral / near, kSpiralMost);
    const float c = std::cos(turn), s = std::sin(turn);
    p.way = {p.way[0] * c + p.way[2] * s, p.way[1], p.way[2] * c - p.way[0] * s};
    p.at = {middle[0] + p.way[0] * near, middle[1] + p.way[1] * near, middle[2] + p.way[2] * near};
    return true;
}

// A prize of the file made at a place, thrown up and any way (PRIZE::appear)
void appear(const PrizeSize &size, const Vec3 &at)
{
    Prize p;
    p.size = &size;
    p.at = at;
    p.speed[0] = between(-20.0F, 20.0F);
    p.speed[1] = between(-40.0F, -20.0F);
    p.speed[2] = between(-20.0F, 20.0F);
    prizes.push_back(p);
}
} // namespace

// Morale orbs worth `value` of the mission's gauge from a place (PRIZE_MU::AppearGauge): as many of each size, the
// largest first, as make its share of what is left to give
void scatter_gauge_prizes(const Vec3 &at, float value)
{
    KindFiles *files = prize_files(kOrbFile, {kOrbs[0].land, kOrbs[0].take});
    if (files == nullptr || enemies.empty())
        return;
    Enemy *e = prize_holder();
    if (e == nullptr) {
        e = start_ally(*files, at, 0.0F, enemies.begin()->first, true, 0);
        holder = e->days;
        // the one effect they all are (YS::MASSEFFECT::MANAGER's: ryj::EFFECT::set_reference_pos each tick)
        if (Enemy::Running *r = the_field().running_effect(cast(*e, 0, 0, false, nullptr)))
            r->prizes = true;
    }
    for (std::size_t i = 0; i < std::size(kOrbs); ++i) {
        const int n = int(value * kOrbShares[i] / kOrbs[i].worth);
        for (int k = 0; k < n; ++k)
            appear(kOrbs[i], at);
        value -= kOrbs[i].worth * float(n);
        if (tracing() && n > 0)
            khdays_log("kh2 enemies: %d orbs of %.1f scattered at %.0f %.0f %.0f (KH2's cm)", n, double(kOrbs[i].worth),
                       double(at[0]), double(at[1]), double(at[2]));
    }
}

// A tick of them (KH2's 60 a second), each one's sprite smaller in its last twelve and as it is taken
void prizes_tick()
{
    Enemy *e = prizes.empty() ? nullptr : prize_holder();
    if (e == nullptr) {
        prizes.clear();
        return;
    }
    for (std::size_t i = prizes.size(); i-- > 0;) {
        Prize &p = prizes[i];
        float size = 1.0F;
        bool on = true;
        if (p.phase == Phase::taken) {
            on = spiral(p, *e);
            size = p.life / kTakenFor;
        } else {
            if (p.phase == Phase::moving)
                move(p, *e);
            else if (p.life >= kPrizeFading)
                recheck(p, *e);
            if (p.life < kPrizeFading)
                size = p.life / kPrizeFading;
            else
                take(p, *e);
            if (p.phase != Phase::taken)
                on = (p.life -= 1.0F) > 0.0F;
        }
        p.shown = std::uint8_t(std::clamp(size, 0.0F, 1.0F) * 255.0F);
        if (!on)
            prizes.erase(prizes.begin() + std::ptrdiff_t(i));
    }
}

// The prizes given to their effect as KH2's are (MASSEFFECT::make_reference: their places and kinds, their sprites'
// sizes and frames), and the frames it moved them on to taken back (MASSEFFECT::exec)
void give_prizes(const Enemy &e, Enemy::Running &r)
{
    std::vector<kh2::EffectPoint> points(prizes.size());
    for (std::size_t i = 0; i < prizes.size(); ++i) {
        const Vec3 at = to_effect_space(e, prizes[i].at, false);
        points[i].place = {at[0], at[1], at[2]};
        points[i].bytes = {prizes[i].size->kind, 0x80, prizes[i].shown, prizes[i].frame};
    }
    r.fx->set_points(std::move(points));
}

void take_prizes(Enemy::Running &r)
{
    const std::vector<kh2::EffectPoint> &back = r.fx->points();
    for (std::size_t i = 0; i < back.size() && i < prizes.size(); ++i)
        prizes[i].frame = back[i].bytes[3];
}

// The prizes gone with the ally whose effects they are (0: the area left)
void drop_prizes(unsigned of)
{
    if (of != 0 && of != holder)
        return;
    prizes.clear();
    holder = 0;
}

} // namespace khdays::kh2_enemies::shell
