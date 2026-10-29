/* A native sequence image's GPU commands. Words, copied into a frame package; no game pointers. */
#ifndef KHDAYS_SEQUENCE_IMAGE_H
#define KHDAYS_SEQUENCE_IMAGE_H
#define KSI_3D_PROGRAMS 9u /* five reticles, four heart-counter batches */
#define KSI_HEADER 40u
#define KSI_QUAD_WORDS 24u
#define KSI_MAX_QUADS 128u
#define KSI_WORDS (KSI_HEADER + KSI_QUAD_WORDS * KSI_MAX_QUADS)
#define KSI_OUTPUT 0u
#define KSI_ATLAS 1u
#define KSI_LOGICAL_W 2u
#define KSI_LOGICAL_H 3u
#define KSI_WIDTH 4u
#define KSI_HEIGHT 5u
#define KSI_VIEW_X 6u
#define KSI_VIEW_Y 7u
#define KSI_SCALE 8u
#define KSI_COUNT 9u
#define KSI_FACE 10u
#define KSI_HD_FACE 11u
#define KSI_FACE_X 12u
#define KSI_FACE_Y 13u
#define KSI_FACE_W 14u
#define KSI_FACE_H 15u
#define KSI_CLEAR 16u    /* a valid transparent portrait frame still clears its previous image */
#define KSI_ANCHOR_X 17u /* native composition anchor; independent of image bounds and mod offsets */
#define KSI_ANCHOR_Y 18u
#define KSI_ANCHORED 19u
#define KSI_FLAGS 20u
#define KSI_DEPTH 21u
#define KSI_BLEND 22u
#define KSI_TEXT_INSERT 23u /* 0: legacy face on top; otherwise insert before quad (value - 1) */
#define KSI_TEXT_SCALE 24u
#define KSI_TEXT_CLIP_X 25u
#define KSI_TEXT_CLIP_Y 26u
#define KSI_TEXT_CLIP_W 27u
#define KSI_TEXT_CLIP_H 28u
#define KSI_TEXT_ALPHA 29u
#define KSI_RADAR_MASK 30u /* radar DDS replacement id + 1, else ordinary sequence */
#define KSI_RADAR_ICONS 31u
#define KSI_RADAR_ICON_W 32u
#define KSI_RADAR_ICON_H 33u
#define KSI_RADAR_SCALE 34u
#define KSI_RADAR_ALPHA 35u
#define KSI_PANEL_ANCHOR 36u /* 0: native placement; 1..9: row-major screen anchors, top-left to bottom-right */
/* A quad: four x,y,u,v,RGBA vertices, then flags (test/write/colour/texture/filter), depth, blend. */
#ifndef KSI_SHADER
typedef struct KhdaysSequenceImage {
    unsigned words[KSI_WORDS];
} KhdaysSequenceImage;
#endif
#endif
