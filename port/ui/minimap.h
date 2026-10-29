/* Minimap-only sprite capture. Native stamping remains intact for exact disabled/error fallback. */
#ifndef KHDAYS_UI_MINIMAP_H
#define KHDAYS_UI_MINIMAP_H
#include "../gfx/sequence_image.h"
#define KHDAYS_MINIMAP_BATCHES 4
#ifdef __cplusplus
extern "C" {
#endif
void khdays_minimap_begin(void);
void khdays_minimap_end(void);
void khdays_minimap_icon(int kind, int x, int y);
void khdays_minimap_player(int player, int x, int y, int heading);
void khdays_minimap_emblem(int player);
void khdays_minimap_stamp(unsigned source, int x, int y);
void khdays_minimap_reset(void);
void khdays_minimap_frame(unsigned frame);
void khdays_minimap_emit(void);
int khdays_minimap_cell(int engine, int bg, unsigned entry);
void khdays_minimap_commands(KhdaysSequenceImage *out);
void khdays_minimap_radar_commands(KhdaysSequenceImage *out);
#ifdef __cplusplus
}
#endif
#endif
