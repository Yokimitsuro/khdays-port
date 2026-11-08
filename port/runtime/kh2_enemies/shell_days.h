/* The Days side of a KH2 enemy's shell (shell.cpp): the Days enemy standing in for it read and moved, the players
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

/* Its model put at `place` (its collision sphere with it) and turned by `yaw` (radians, KH2's: atan2(x, z)); the
 * room's collision then keeps it out of the walls and on the ground (Ov107_AiState_ResolveContacts, later in the same
 * tick). */
void khdays_kh2_shell_place(unsigned actor, const float place[3], float yaw);

/* A point of Days's world (metres) put in the game's memory as its vectors are (fx32 x3), for its own code to read:
 * the place it lies in, 0 when there is no room for it. The same place each call. */
unsigned khdays_kh2_shell_point(const float place[3]);

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
 * along `push` (a unit way): Ov107_InvokeHitCallback, as Days's enemies' attacks hit (Ov120_SweepAttack_Step).
 * Whether the hit was taken. */
int khdays_kh2_shell_hit(unsigned actor, unsigned victim, int slot, const float push[3]);

/* Its first attack slot with a power (Ov107_SetAttackPower's, +0x290 + 6 n), 0 when none has. */
int khdays_kh2_shell_attack_slot(unsigned actor);

#ifdef __cplusplus
}
#endif

#endif
