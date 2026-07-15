#pragma once

#include <map>
#include <string>
#include <vector>

#include "khdays/assets/graphics2d.h"  // Tilemap, TileGraphics, Palette2D
#include "khdays/assets/mesh.h"        // NeutralModel
#include "khdays/assets/tex0.h"        // DecodedTexture

// A DS-style 2D screen compositor: the native form of how the hardware builds a
// frame from several tiled background layers and OBJ sprites, ordered by
// priority. Scenes reproduce a DS screen by describing its layers (which the
// decompiled scene constructors program into the BG/OBJ registers) and letting
// this compose them to RGBA — instead of inventing a single-image layout.
namespace khdays::assets {

// One tiled background layer (a decoded NSCR + its NCGR + NCLR), with the DS BG
// draw priority and pixel scroll the scene programs into the BG control regs.
struct ScreenBgLayer final {
    const Tilemap* map = nullptr;
    const TileGraphics* tiles = nullptr;
    const Palette2D* palette = nullptr;
    int priority = 0;    // DS BG priority 0..3 (0 = frontmost)
    int scroll_x = 0;    // pixels; wraps around the tilemap
    int scroll_y = 0;
};

// One OBJ sprite: a pre-rendered cell image (e.g. from render_cell) placed at
// (x, y) in screen space, with its OBJ priority.
struct ScreenObj final {
    const DecodedTexture* image = nullptr;
    int x = 0;
    int y = 0;
    int priority = 0;    // DS OBJ priority 0..3 (0 = frontmost)
};

// Composite the BG layers and OBJ sprites into a width x height RGBA frame using
// DS priority order: higher priority value is further back, so drawing runs from
// priority 3 (back) to 0 (front); at each level the BG layers are drawn first,
// then the OBJ sprites (OBJ beats BG at equal priority). Palette index 0 is
// transparent for BG tiles; OBJ images keep their own alpha. The backdrop is
// opaque black. This is a faithful text-BG + OBJ model (no affine/3D BG yet).
DecodedTexture compose_screen(
    const std::vector<ScreenBgLayer>& bg_layers,
    const std::vector<ScreenObj>& objects,
    int width = 256,
    int height = 192);

// Composite a flat (2D-in-3D) model — e.g. the title logo, which is a handful of
// textured quads facing the camera — to a `width`x`height` RGBA image. The
// model's rest-pose XY is scaled to fill `fill` of the canvas (preserving aspect,
// centred horizontally, top-aligned by `top_margin` fraction), each triangle is
// textured from `textures` (keyed by mesh texture name) and modulated by the
// vertex colour, and meshes are drawn back-to-front by depth. Transparent
// backdrop.
DecodedTexture compose_flat_model(
    const NeutralModel& model,
    const std::map<std::string, DecodedTexture>& textures,
    int width = 256,
    int height = 192,
    float fill = 0.86F,
    float top_margin = 0.06F);

}  // namespace khdays::assets
