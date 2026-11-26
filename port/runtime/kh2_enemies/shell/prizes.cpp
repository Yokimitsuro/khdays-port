// KH2's prizes (YS::PRIZEOBJ, YS::PRIZEEFFECT, the PS3 build's): what a script scatters, how it flies, lies and is
// taken.
#include "internal.h"

namespace khdays::kh2_enemies::shell {
namespace {

// One size of a file's prizes (PRIZEEFFECT::PARAM): its sprite in the file's effect, how far under its place it
// stands on the ground (KH2's cm), its sounds as it bounces and as it is taken, and what it is worth
struct PrizeSize final {
    std::uint8_t kind;
    float height;
    int land, take;
    float worth;
};

constexpr int kLandSound = 41, kTakeSound = 42; // KH2's common bank's
// The morale orbs (PRIZE_MU), the largest first, and the share of what is left to give each size takes
// (PRIZE_MU::AppearGauge); the Hostile Program's data (PRIZE_TR)
constexpr PrizeSize kOrbs[] = {{0, 40.3F, kLandSound, kTakeSound, 2.0F},
                               {1, 27.9F, kLandSound, kTakeSound, 0.5F},
                               {2, 18.6F, kLandSound, kTakeSound, 0.1F}};
constexpr float kOrbShares[] = {0.9F, 0.9F, 1.0F};
constexpr PrizeSize kData = {0, 31.0F, kLandSound, kTakeSound, 1.0F};
constexpr const char *kFiles[] = {"PRIZE_MU", "PRIZE_TR"}; // by PrizeKind
enum PrizeKind { kOrbPrize, kDataPrize, kPrizeKinds };

// How long they last, fade and spiral in once taken (KH2's ticks, 60 a second), how many bounces are heard, how often
// the ground under one lying is looked for
constexpr float kPrizeLife = 900.0F, kPrizeFading = 12.0F, kTakenFor = 40.0F;
constexpr int kBouncesHeard = 2, kRecheck = 30;
// How they fly (KH2's cm and ticks): thrown up and across at random, pulled down, slowed, bounced back up while fast
constexpr float kThrowAcross = 20.0F, kThrowUpLeast = 20.0F, kThrowUpMost = 40.0F;
constexpr float kPrizeRadius = 55.0F; // its collision's (PRIZEEFFECT::Init)
constexpr float kPrizeGravity = 3.0F, kPrizeFall = 50.0F, kPrizeDrag = 0.95F;
constexpr float kPrizeBounce = -0.8F, kPrizeRest = 5.0F; // slower than this it lies
constexpr float kWallTurn = 0.1F;                        // a wall that changed its way by more turns it back
constexpr float kGroundAbove = 5.0F;                     // a lying prize's ground looked for this far under its foot
// How near a player takes it (PRIZE::CanGet), and how it spirals into him
constexpr float kReachAcross = 120.0F, kReachUp = 30.0F;
constexpr float kSpiral = 30.0F, kSpiralMost = 0.59341F; // how far round it goes a tick, at most (34 degrees)
// Its sprite's bytes for the effect (EffectPoint::bytes): its kind, its alpha, its size and its frame (/ 255; a frame of
// kAnyFrame one at random)
constexpr std::uint8_t kSpriteAlpha = 0x80, kWholeSize = 255, kAnyFrame = 255;

enum class Phase { moving, lying, taken };

struct Prize final {
    PrizeKind kind = kOrbPrize;
    const PrizeSize *size = nullptr;
    Vec3 at{}, speed{}; // its place and its way a tick (KH2's cm)
    float life = kPrizeLife;
    int bounces = 0, recheck = kRecheck;
    bool ready = false; // once it falls: can be taken
    Phase phase = Phase::moving;
    unsigned player = 0;      // the Days player who took it (his object), 0 a KH2 object
    std::uint32_t object = 0; // which took it (its handle)
    Vec3 way{};               // the way from its taker's middle to it as it was taken, one long, and how far it was
    float far = 0.0F;
    std::uint8_t shown = kWholeSize, frame = kAnyFrame; // its sprite's size and frame
};

std::vector<Prize> prizes;
std::array<unsigned, kPrizeKinds> holders{}; // the allies whose effects each file's prizes are, 0 none

// A number between two at random, KH2's chance's
float between(float from, float to)
{
    constexpr float kChances = 4294967296.0F; // kh2_chance's: 32 bits
    return from + (to - from) * float(kh2_chance()) / kChances;
}

Enemy *prize_holder(PrizeKind kind)
{
    const auto it = allies.find(holders[kind]);
    return it != allies.end() && !it->second->released ? it->second.get() : nullptr;
}

// The ally whose effects a file's prizes are, made at `at` with the battle of the Days enemy `anchor`; null when the
// mod has not the file
Enemy *prize_maker(PrizeKind kind, const Vec3 &at, unsigned anchor)
{
    if (Enemy *e = prize_holder(kind))
        return e;
    KindFiles *files = prize_files(kFiles[kind], {kLandSound, kTakeSound});
    if (files == nullptr)
        return nullptr;
    Enemy *e = start_ally(*files, at, 0.0F, anchor, true, 0);
    holders[kind] = e->days;
    // the one effect they all are (YS::MASSEFFECT::MANAGER's: ryj::EFFECT::set_reference_pos each tick)
    if (Enemy::Running *r = the_field().running_effect(cast(*e, 0, 0, false, nullptr)))
        r->prizes = kind + 1;
    return e;
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
    to_days({next[0], std::max(foot[1], next[1]) + 1.0F, next[2]}, to); // (a cm past where it falls to)
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
    if (std::fabs(way[0] - p.speed[0]) > kWallTurn && p.phase == Phase::moving)
        p.speed[0] = -p.speed[0];
    if (std::fabs(way[2] - p.speed[2]) > kWallTurn && p.phase == Phase::moving)
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
    to_days({p.at[0], foot + kGroundAbove, p.at[2]}, to);
    if (!khdays_kh2_shell_ray(shell_of(e), from, to, hit))
        p.phase = Phase::moving;
}

// An object takes it (PRIZEEFFECT::get): from now on round its middle
void taken(Prize &p, Object &by, const Enemy &e)
{
    Vec3 middle = by.place();
    the_field().aim_point(by, middle);
    const Vec3 d = {p.at[0] - middle[0], p.at[1] - middle[1], p.at[2] - middle[2]};
    p.far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    p.way = p.far > 0.0F ? Vec3{d[0] / p.far, d[1] / p.far, d[2] / p.far} : Vec3{};
    p.object = by.address();
    p.phase = Phase::taken;
    p.life = kTakenFor;
    sound(e, p.size->take);
}

// A Days player near enough takes it (PRIZE::CanGet), its worth added to the mission's gauge (PRIZE::CheckGetGauge)
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
        p.player = player.object;
        taken(p, o, e);
        the_field().mission_gauge(kAddGauge, 0, p.size->worth);
        if (tracing())
            khdays_log("kh2 enemies: player %08x takes a %s worth %.1f of the mission's gauge", player.object,
                       kFiles[p.kind], double(p.size->worth));
        return;
    }
}

// Round its taker's middle, nearer each tick, till it is in it (PRIZEEFFECT::phase_get); whether it goes on
bool spiral(Prize &p, const Enemy &e)
{
    p.life -= 1.0F;
    Object *by = Object::at(mod().memory, p.object);
    if (p.life <= 0.0F || by == nullptr)
        return false;
    if (p.player != 0) {
        std::vector<KhdaysKh2Player> list(std::size_t(std::max(0, khdays_kh2_shell_players(shell_of(e), nullptr, 0))));
        const int n = khdays_kh2_shell_players(shell_of(e), list.data(), int(list.size()));
        const auto end = list.begin() + std::min(n, int(list.size()));
        const auto at =
            std::find_if(list.begin(), end, [&p](const KhdaysKh2Player &x) { return x.object == p.player; });
        if (at == end)
            return false;
        by->set_place(to_kh2(at->place));
    }
    Vec3 middle = by->place();
    the_field().aim_point(*by, middle);
    const float near = p.far * p.life / kTakenFor, turn = std::min(kSpiral / near, kSpiralMost);
    const float c = std::cos(turn), s = std::sin(turn);
    p.way = {p.way[0] * c + p.way[2] * s, p.way[1], p.way[2] * c - p.way[0] * s};
    p.at = {middle[0] + p.way[0] * near, middle[1] + p.way[1] * near, middle[2] + p.way[2] * near};
    return true;
}

// A prize of a file made at a place, thrown up and any way (PRIZE::appear)
void appear(PrizeKind kind, const PrizeSize &size, const Vec3 &at)
{
    Prize p;
    p.kind = kind;
    p.size = &size;
    p.at = at;
    p.speed[0] = between(-kThrowAcross, kThrowAcross);
    p.speed[1] = between(-kThrowUpMost, -kThrowUpLeast); // (KH2's y down)
    p.speed[2] = between(-kThrowAcross, kThrowAcross);
    prizes.push_back(p);
}
} // namespace

// Morale orbs worth `value` of the mission's gauge from a place (PRIZE_MU::AppearGauge): as many of each size, the
// largest first, as make its share of what is left to give
void scatter_gauge_prizes(const Vec3 &at, float value)
{
    if (enemies.empty() || prize_maker(kOrbPrize, at, enemies.begin()->first) == nullptr)
        return;
    for (std::size_t i = 0; i < std::size(kOrbs); ++i) {
        const int n = int(value * kOrbShares[i] / kOrbs[i].worth);
        for (int k = 0; k < n; ++k)
            appear(kOrbPrize, kOrbs[i], at);
        value -= kOrbs[i].worth * float(n);
        if (tracing() && n > 0)
            khdays_log("kh2 enemies: %d orbs of %.1f scattered at %.0f %.0f %.0f (KH2's cm)", n, double(kOrbs[i].worth),
                       double(at[0]), double(at[1]), double(at[2]));
    }
}

// The prizes KH2's area places with a battle (a spawn's entry of kind kEntryPrizes: the Hostile Program's data): made
// then; without them none of that file appear
bool place_prizes(const std::string &name, const Vec3 &at, unsigned anchor)
{
    for (int kind = 0; kind < kPrizeKinds; ++kind)
        if (name == kFiles[kind])
            return prize_maker(PrizeKind(kind), at, anchor) != nullptr;
    return false;
}

// So many of the Hostile Program's data from a place (PRIZE_TR::AppearNum); all not taken yet taken by an object
// (PRIZE_TR::GetAll), worth nothing to the gauge then
void scatter_data_prizes(const Vec3 &at, int n)
{
    if (prize_holder(kDataPrize) == nullptr)
        return;
    for (int k = 0; k < n; ++k)
        appear(kDataPrize, kData, at);
    if (tracing() && n > 0)
        khdays_log("kh2 enemies: %d data scattered at %.0f %.0f %.0f (KH2's cm)", n, double(at[0]), double(at[1]),
                   double(at[2]));
}

void take_data_prizes(Object &by)
{
    const Enemy *e = prize_holder(kDataPrize);
    if (e == nullptr)
        return;
    int n = 0;
    for (Prize &p : prizes)
        if (p.kind == kDataPrize && p.phase != Phase::taken) {
            taken(p, by, *e);
            ++n;
        }
    if (tracing())
        khdays_log("kh2 enemies: %d data taken by %08x", n, by.address());
}

// A tick of them (KH2's 60 a second), each one's sprite smaller as it fades and as it is taken
void prizes_tick()
{
    for (std::size_t i = prizes.size(); i-- > 0;) {
        Prize &p = prizes[i];
        Enemy *e = prize_holder(p.kind);
        if (e == nullptr) {
            prizes.erase(prizes.begin() + std::ptrdiff_t(i));
            continue;
        }
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
        p.shown = std::uint8_t(std::clamp(size, 0.0F, 1.0F) * float(kWholeSize));
        if (!on)
            prizes.erase(prizes.begin() + std::ptrdiff_t(i));
    }
}

// The prizes given to their effect as KH2's are (MASSEFFECT::make_reference: their places and kinds, their sprites'
// sizes and frames), and the frames it moved them on to taken back (MASSEFFECT::exec)
void give_prizes(const Enemy &e, Enemy::Running &r)
{
    std::vector<kh2::EffectPoint> points;
    for (const Prize &p : prizes)
        if (p.kind + 1 == r.prizes) {
            const Vec3 at = to_effect_space(e, p.at, false);
            kh2::EffectPoint point;
            point.place = {at[0], at[1], at[2]};
            point.bytes = {p.size->kind, kSpriteAlpha, p.shown, p.frame};
            points.push_back(point);
        }
    r.fx->set_points(std::move(points));
}

void take_prizes(Enemy::Running &r)
{
    const std::vector<kh2::EffectPoint> &back = r.fx->points();
    std::size_t i = 0;
    for (Prize &p : prizes)
        if (p.kind + 1 == r.prizes && i < back.size())
            p.frame = back[i++].bytes[3];
}

// The prizes gone with the ally whose effects they are (0: the area left)
void drop_prizes(unsigned of)
{
    for (int kind = 0; kind < kPrizeKinds; ++kind)
        if (of == 0 || of == holders[kind]) {
            std::erase_if(prizes, [kind](const Prize &p) { return p.kind == kind; });
            holders[kind] = 0;
        }
}

} // namespace khdays::kh2_enemies::shell
