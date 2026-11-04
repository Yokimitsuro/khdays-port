/* Where the KH2 camera (kh2_camera.c) meets the game's field camera: prepare.py's HOOKS call these from
 * Ov002_Camera_UpdateFollow, Ov002_TickCamera and Ov002_TickCameraTransition. */
#ifndef KHDAYS_KH2_CAMERA_HOOKS_H
#define KHDAYS_KH2_CAMERA_HOOKS_H

/* Whether the option is on and this is the gameplay camera, from the camera's flags (+0x38, +0x3c)
 * and mode (+0x40): Controls Type A, no script's camera, the first screen's. The Select view is its too. */
int khdays_kh2_camera_claims(int flags, int flags2, int mode);

/* Ov002_Camera_UpdateFollow, before it picks its next handler, `camera` a DS address: this frame is the KH2 camera's,
 * and the game's camera reset (flag 8) is too, so it does not start Ov002_TickLockedCamera. */
void khdays_kh2_camera_follow(int camera, int input_off, int bits);

/* Ov002_TickCamera, locked on to the point at DS address `target`. */
void khdays_kh2_camera_lock(int target, int camera);

/* Whether the last game frames were this camera's with the player in control of it, and the player's own press that
 * centres it, which the input layer takes from R while lock-on has its own buttons (input.c). */
int khdays_kh2_camera_active(void);
void khdays_kh2_camera_center(void);

/* First person (KHDAYS_ENH_KH2_FIRST_PERSON): the input layer's Select. Every steer's clamp
 * (Ov022_ClampAngleTowardTarget) asks whether `actor` strafes, going where the stick says. The draws ask for the scene node
 * of the player being faded and how far (0 when none is, runtime/display/character_shadows.c). */
void khdays_kh2_camera_toggle_first_person(void);

/* Ov002_ReaimActor: the camera is about to be committed; while the game's tick is gated this is its only frame. */
void khdays_kh2_camera_commit(int camera);

/* Ov002_Camera_UpdateFollow, each frame: a hold (camera flag 0x200) is still this camera's. */
void khdays_kh2_camera_held(int camera);

/* Ov022_EnterActorState: the local player's actor enters `state`. */
void khdays_kh2_camera_player_state(int state);
int khdays_kh2_camera_strafing(int actor);
unsigned khdays_kh2_camera_faded_node(float *fade);

/* Ov002_TickCameraTransition, before the shake: this frame's eye and look-at in the camera's fields, when it is this
 * camera's. `player`, `origin` and `eye` are DS addresses of vectors, the last two free for it to use. */
void khdays_kh2_camera_frame(int camera, int player, int behind_yaw, int origin, int eye);

#endif
