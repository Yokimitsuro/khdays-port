/* KINGDOM HEARTS II's Dark Corridor in place of Days's standing ovals: the effect the KH2 Prizes mod
 * copies from the player's own KH2 (mods/<mod>/kh2fx/tt_event_904.pax, KH2's event/effect/tt_event_904.pax, with
 * kh2fx/texcommon.dpd, its effect/texcommon.dpd, and their HD pictures kh2fx/<file's stem>/-<n>.dds) played as KH2
 * plays it (assets/kh2_effect.h) where the game draws one of its ovals (mi/ob/'s E000_xx_000 and E_100_xx_000), its
 * floor on the oval's and as tall as the oval is seen (kh2_corridor_fit.h): opening when the oval shows, holding
 * while it is drawn, closing within the oval's own closing clip so as to end with it, or where the oval was once it
 * is no longer drawn (docs/RUNTIME_MAP.md, "The Dark Corridors"). The setting kh2_corridors (Enhancements) leaves
 * Days's own from the next oval on. Main thread, from runtime/mods/models.cpp and the 3D lists' SwapBuffers. */
#ifndef KHDAYS_KH2_CORRIDORS_H
#define KHDAYS_KH2_CORRIDORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* A model's draw begins (khdays_models_draw_begin, after khdays_g3d_draw_begin): when `resource` is one of the
 * ovals and its corridor is drawn instead -- the setting on, the effect there and readable, or its corridor already
 * open -- its corridor is run to the game's frame and drawn at the matrices now, and 1 is returned: the model's own
 * shapes are then left out. 0: the game's own drawn. */
int khdays_kh2_corridors_begin(const void *resource, const void *render_obj);

/* For the fit's measurements, with KHDAYS_TRACE_CORRIDORS=2 (the session log): the oval's node `node` placed by its
 * draw (khdays_models_node) -- its size and, once a second, its matrix under the oval's own: the oval's pose as Days
 * draws it -- and one of its shapes reached (khdays_models_shape), with its alpha. =3 also logs KH2's camera as the
 * effect sees it (to draw it with a reference player from the game's point of view) and every model's place in the
 * world (where the ground is beside an oval). */
void khdays_kh2_corridors_node(unsigned node);
void khdays_kh2_corridors_shape(void);

#ifdef __cplusplus
}
#endif

#endif
