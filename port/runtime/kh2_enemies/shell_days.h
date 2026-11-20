/* The Days side of a KH2 enemy's shell (shell/): the Days enemy standing in for it read and moved, the players
 * near it, its hits on them -- the game's fields and routines, as runtime/scripts/enemies_link.c reaches them
 * (docs/KH2_ENEMIES.md, "Days's side: the shell"). Places are Days's units (the DS's 0x1000 = 1.0). */
#ifndef KHDAYS_KH2_SHELL_DAYS_H
#define KHDAYS_KH2_SHELL_DAYS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysKh2Shell {
    float place[3]; /* its model's place (srt's translation) */
    float above;    /* its collision sphere's height over the ground under it, less its radius (0 standing on it);
                        -1 with no ground under it */
    float radius;   /* its collision sphere's */
    int standing;   /* set going and not defeated (+0x60's 0x100) */
    int own;        /* this game's simulation's own (+0x50 1), not a room guest's copy of the host's */
    int hp, hp_max;
    int kind; /* its Days class (+0x19c) */
} KhdaysKh2Shell;

/* The Days enemy at `actor` (a DS address); 0 when it is not one in the game's memory. */
int khdays_kh2_shell_read(unsigned actor, KhdaysKh2Shell *out);

/* Its HP made `hp` (kept between 1 and its most: Days's defeat comes through its hits), as KH2's script sets it. */
void khdays_kh2_shell_set_hp(unsigned actor, int hp);

/* Its most HP made `most` (at least 1) and its HP `hp` (kept between 1 and it): the share of its KH2 health the shell
 * keeps shown in Days's own fields (signed 16 bits each: their HUD bar and their hits). */
void khdays_kh2_shell_set_health(unsigned actor, int hp, int most);

/* Its model put at `place` (its collision sphere with it) and turned by `yaw` (radians about Days's up: +z to (sin, 0,
 * cos) of it -- a KH2 yaw's days_turn, shell/internal.h); the room's collision then keeps it out of the walls and on
 * the ground (Ov107_AiState_ResolveContacts, later in the same tick). */
void khdays_kh2_shell_place(unsigned actor, const float place[3], float yaw);

/* A point of Days's world (metres) put in the game's memory as its vectors are (fx32 x3), for its own code to read:
 * the place it lies in, 0 when there is no room for it. The same place each call within an area. */
unsigned khdays_kh2_shell_point(const float place[3]);

/* The area is left (khdays_kh2_enemies_area_left, while the field's heap still stands): the point's place given back. */
void khdays_kh2_shell_area_left(void);

/* Its collision sphere's radius and its centre's height over its place (+ 0x70, + 0x64's y; Days makes the sphere of
 * them each frame, Ov107_UpdateCollisionSphere): what the room's collision, the players' pushes and Days's hits use.
 * The old ones given back in `old_radius`, `old_centre` when not null. */
void khdays_kh2_shell_size(unsigned actor, float radius, float centre, float *old_radius, float *old_centre);

/* The players standing near it (the field's players, Ov107_FindNearestObject's list): up to `room` into `out` (NULL
 * to count) as {object, place (its collision sphere's centre less its radius: its feet), radius}; how many. */
typedef struct KhdaysKh2Player {
    unsigned object;
    float place[3];
    float radius;
} KhdaysKh2Player;
int khdays_kh2_shell_players(unsigned actor, KhdaysKh2Player *out, int room);

/* `victim` (a player's object) hit by the enemy at `actor` with its attack `slot` (+0x28c: its power, kind), pushed
 * along `push` (a unit way): Ov107_InvokeHitCallback, as Days's enemies' attacks hit (Ov120_SweepAttack_Step);
 * `unguarded` one his block does not stop (the request's 8: Ov022_TryBlockHit lets it through). Whether the hit was
 * taken. */
int khdays_kh2_shell_hit(unsigned actor, unsigned victim, int slot, const float push[3], int unguarded);

/* The player's actor (Ov022_ActorUpdate's) staggered and pushed along `push` with no damage nor hit counted: what
 * Ov022_ReceiveHit ends with (its flag 0x10, its flags2's 0x100, the push his drift), on the host alone -- a KH2
 * attack's reflection throws him back (YS::PARTY::_OVR_exec_reflect); Days's own hit would count it taken (its
 * World_AddStat, a mission's count of hits) and take HP. Nothing when he has none left. */
void khdays_kh2_shell_player_recoil(unsigned actor, const float push[3]);

/* A segment of Days's world (from, to: metres) against the room's collision the enemy at `actor` stands in (its
 * scene's, + 0x4's + 0x7c, as Ov107_AiState_ResolveContacts and Collision_ProbeGround cast it): where it first meets
 * it in `hit`; whether it does. Days's collision keeps three lists, each met by its own cast, as the camera's line
 * meets them: Collision_RunRayCast its floors and slopes (Coll_WalkTreeForHit reads a node's first list alone),
 * Collision_RunSphereCast with no radius its walls (the second), Collision_CastNearest its ceilings -- the nearest of
 * the three (the ray alone meets no wall). */
int khdays_kh2_shell_ray(unsigned actor, const float from[3], const float to[3], float hit[3]);
/* The same, and the face it meets in `face` (the collision's own record, while the room stands: KH2's 1:135 gives
 * its polygon so, 1:137 its normal; may be NULL). With `attacks`, as KH2's attacks see the room: a face named nohit
 * (a wall characters alone meet: Days's invisible walls, as Ov022_CastMove's parts go through them, and a KH2 map's
 * made into a room) is gone through */
int khdays_kh2_shell_ray_face(unsigned actor, const float from[3], const float to[3], int attacks, float hit[3],
                              unsigned *face);
/* A face's plane normal (a unit way, Days's axes) from its record; whether it has one */
int khdays_kh2_shell_face_normal(unsigned face, float normal[3]);

/* The same segment's sphere of `radius` (metres) against the room's walls (Collision_RunSphereCast: the second list,
 * Actor_ResolveMove's walls), the collision's objects left out: where the sphere's centre is as it first meets one in
 * `hit`, that face's normal in `normal` (a unit way); whether it does. */
int khdays_kh2_shell_wall(unsigned actor, const float from[3], const float to[3], float radius, float hit[3],
                          float normal[3]);

/* The story's difficulty (the game state's field 0x40a, two bits: 0 Beginner, 1 Standard, 2 Proud, 3 Critical). */
int khdays_kh2_shell_difficulty(void);

/* Its first attack slot with a power (Ov107_SetAttackPower's, +0x290 + 6 n), 0 when none has. */
int khdays_kh2_shell_attack_slot(unsigned actor);

/* A player's actor (Ov022_ActorUpdate's): its field object (+0x4ec, the one the players' list holds), 0 none; in
 * `local` whether it is this game's own player's (its owner, +8, the session's local player), in `node` its model's
 * scene node (+0x20: what the models' draws know it by). Either may be NULL. */
unsigned khdays_kh2_shell_player_object(unsigned actor, int *local, unsigned *node);

/* A player's actor put at `place` (its feet; its node with it, Actor_SetVecAndSyncChild) and turned by `yaw`
 * (radians about Days's up: its model looks along (sin, 0, cos) of it, as Days's node angle at + 0x80 does -- a KH2
 * yaw's days_turn, shell/internal.h). */
void khdays_kh2_shell_put_player(unsigned actor, const float place[3], float yaw);

/* A player's actor's HP (+0x12, Ov022_ActorUpdate's nHp), -1 when it is not in the game's memory. */
int khdays_kh2_shell_player_hp(unsigned actor);
/* Its most HP (+0x16, nHpMax), -1 the same. */
int khdays_kh2_shell_player_most_hp(unsigned actor);
/* Its level: its member record's (gPartyMembers + 0x104 x its nId, + 0x9: + 2, the level less 1), 0 when not known (the
 * record not made: its + 6 0) -- what KH2's player's sheet gives a script (1:361, YS::SHEET::get_level). */
int khdays_kh2_shell_player_level(unsigned actor);
/* Its HP made `hp` (Ov022_ActorSetHp: kept to its most, its knock-out at 0), on the host alone -- KH2's script adding
 * to its player's health (1:155: Sephiroth's Heartless Angel leaves him 1). */
void khdays_kh2_shell_player_set_hp(unsigned actor, int hp);

/* A player's actor's pad this frame, as Ov022_TranslateCommandToAction reads it before it locks him: the buttons pressed
 * (+0x18: 1 the command, 0x800 the jump) in `pressed`, the way his D-pad holds in `heading` -- the game's own heading
 * (Ov022_HeadingFromHeldDpad, the stick's angle with analog movement) turned by the camera's (+0x478 nAngleBias), Days's
 * 0x10000 a turn, along (-sin, -cos) of it; -1 none held. Whether it is in the game's memory. */
int khdays_kh2_shell_player_pad(unsigned actor, unsigned *pressed, int *heading);

/* A player's actor's model left undrawn or drawn again: its node's flag 0x20, which the game's draws read and only
 * they (+0x694 bit 0, bSuppressDraw, would also have Ov022_TickSubObjectChain draw his sub objects: a copy of his
 * body standing in its rest pose). His weapons are drawn apart (khdays_kh2_enemies_turned). */
void khdays_kh2_shell_player_hidden(unsigned actor, int hidden);

#ifdef __cplusplus
}
#endif

#endif
