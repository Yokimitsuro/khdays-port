/* A mod's models drawn instead of the game's: mods/<Mod>/graphics/<name>.gltf (or .glb, .fbx, .mdlx), named as
 * the dump names the game's, while HD graphics are on. The game draws as always; the port takes each
 * node's matrix, leaves out the model's shapes and draws the mod's meshes on their own skeleton,
 * joints following nodes by name or <model>.bones.txt (docs/RENDERING.md, "Mods' graphics"). */
#ifndef KHDAYS_MODELS_H
#define KHDAYS_MODELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* NNSi_G3dDrawInternal's start: the render object's joint animation (its NNSG3dAnmObj's reference, 0 none),
 * for the draw that follows (a mod's character's own skeleton plays its clip for it). */
void khdays_models_draw_animation(unsigned animation);

/* NNSi_G3dDrawInternal's start, for `model` (its NNSG3dResMdl) and `render_obj` (its NNSG3dRenderObj:
 * the joint animations it plays, anmJnt at +0x10), and its end. */
void khdays_models_draw_begin(const void *model, const void *render_obj);
void khdays_models_draw_end(void);

/* NNSi_G3dFuncSbc_NODEDESC: node `node`'s matrix is the current one (`sent`:
 * it was sent, not only computed). */
void khdays_models_node(unsigned node, int sent);

/* NNSi_G3dFuncSbc_NODEDESC, node `node`'s matrix made, before its callback reads it: a mod's character's
 * weapon node is put on its own skeleton's weapon bone. */
void khdays_models_node_placed(unsigned node, int sent);
/* Obj_CaptureSelectedJointMtx: node `node`'s matrix was saved at `matrix` (an
 * MtxFx43) for what the game attaches to it; a KH2 model's `weapon` line puts
 * it on a KH2 bone at the draw's end. */
void khdays_models_joint_saved(void *matrix, unsigned node);

/* Mover_ResolveFloor: the mover `mover` (its owner at +0) was put on its floor;
 * its owner's KH2 model's feet are put on that floor (IKAdjust). */
void khdays_models_actor_ground(void *mover);

/* Ov022_EnterActorState: the battle actor `actor` (a DS address) enters state `state`; its KH2 model plays
 * motions.txt's `air` lines in state 5 (damage in the air) and `land` on landing straight out of it. */
void khdays_models_actor_state(int actor, int state);

/* NNSi_G3dFuncSbcSHP, the shape's display list `list` (`size` bytes): whether
 * it is left unsent (the draw's model is replaced). One left unsent still has
 * its matrix commands run (MTX_RESTORE, MTX_SCALE, ...): the nodes after it
 * are placed on the matrix it leaves (a model with a position scale, 051's,
 * scales the matrix in its lists). */
int khdays_models_shape(const void *list, unsigned int size);

/* A mod's model (`path`: glTF, GLB or FBX) drawn at a place of the port's, not instead of a game's model (a
 * prize's box, ui/prize_effects.cpp): `place` its model-to-view matrix (column-major, the position matrix's
 * units), `attr` the polygons' POLYGON_ATTR (alpha, id; no lights: drawn in its own colours). Whether it
 * was drawn (else it cannot be read, named once in the log). */
int khdays_models_draw_placed(const char *path, const float place[16], unsigned int attr);

/* That model's size at rest (its own units): its lowest and highest corners. Whether it can be read. */
int khdays_models_placed_bounds(const char *path, float low[3], float high[3]);

/* The field's dialogue, for KH2 faces (motions.txt's `face` lines): Ov002_SceneApplyPanelRequest shows a
 * panel with `portrait` (its pReq[5], -1 none; Ov002_SceneClosePanelStep: -1, the panel gone), and the
 * crawl typed a letter (text.c). */
void khdays_models_dialogue_speaker(int portrait);
void khdays_models_dialogue_typed(void);

#ifdef __cplusplus
}
#endif

#endif
