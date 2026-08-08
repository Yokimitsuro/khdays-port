# ov000 render-node subsystem — port design

The DS front-end (ov000) does not blit flat 2D screens: it renders a tree of
**nodes** through the geometry engine, each with an animated transform, and each
carrying **object cells** (sprites). The port's game loop currently draws through
a flat 2D `Renderer` (blit RGBA at x,y), which is why the front-end is
reproduced screen-by-screen as static composites plus a few hand-written eases.
This document is the plan to port the real subsystem so the title (and later the
in-game menu, ov008, which shares it) animates faithfully.

Everything here is grounded in the decompilation (ov000 is 100% decompiled) and
in Ghidra; addresses/behaviours are cited so the implementation is a reading,
not a guess.

## What the DS does (measured)

- **RenderNode** (`func_0202aa9c`): builds the node's 3×3/4×3 matrix — a Y/Z
  rotation from a sin/cos table (`MTX_RotY33_`, applied only when node flag bit5
  is set) plus a translation — loads it into the geometry engine
  (`Gfx_SubmitCachedCommandBlock` / `GX_SendFifoWords 0x17`), runs the node's
  animation channels (`Obj_InitChannelsAndRun`), then walks the child-node list
  (link at node+0x62) rendering each. So a node = transform + cells + children.
- **Animation channels** (`Anim_SetFrameWrapped` = `func_01fff774`): a node holds
  several channels (the title uses 0 and 2). Each channel has a current frame in
  **20.12 fixed point** and a length in frames (at `channel[2]+4`, u16). Setting
  a frame past the length wraps it once (`frame -= length << 12`). Channels drive
  the transform/cell over time. `func_0202aef8(node, ch)` returns a channel's
  length; the title's fade-in seeds channels 0 and 2 to `counter << 12` and, on a
  skip, jumps them to `length-1` (`func_ov000_0204e270`).
- **Object cells** (the object manager, `objectManager` at ctx+0x1b0, 0x4a54
  bytes): per cell, `func_020325ec(mgr, cell, frame)` sets the cell's animation
  frame, `func_02032710(mgr, cell, visible)` toggles visibility,
  `func_0203257c(mgr, cell, pos)` positions it. The menu holds **10 cells**
  (`func_ov000_0204cc90`): cell 0 = cursor, cells 1..9 = the options across
  pages; the layout shows/hides them per page and per progression flag.
- **Page scroll** (`func_ov000_02050ec4`, already ported as an ease): eases four
  page positions toward their target (quarter of the gap per frame, snap < 1/8
  px), the selected page offset +8px.
- **Selection pulse** (`func_ov000_0205157c`, already ported): a ping-pong alpha
  tween 2/16..8/16 over 500 ms on the selection highlight.

## Key architectural fact

Most title/menu node transforms are effectively **2D affine** (translation,
scale, and a Z-rotation from the sin/cos table) — not perspective 3D. So the port
does **not** need to emulate the geometry-engine FIFO; it needs a **2D affine
sprite draw** plus the node/channel model. Nodes that set a genuine 3D rotation
(flag bit5 with a non-trivial Y rotation) are the only ones that would need the
GPU path; the title menu does not appear to.

## Port architecture (proposed)

Three layers, matching the port's neutral-format discipline:

1. **`Renderer` gains an affine sprite draw** — `draw_image` already exists (axis
   aligned + alpha). Add `draw_image_affine(rgba, w, h, matrix2x3, alpha)` where
   the 2×3 matrix maps source→screen (translation/scale/rotation).
   - SoftwareRenderer: inverse-map each destination pixel (sample source, alpha
     blend) — the offscreen path.
   - SDL backend: `SDL_RenderTextureRotated` (or `SDL_RenderGeometry` for a full
     2×3) with the texture's alpha mod. GPU path.
2. **Neutral node/animation model** in `khdays-game`:
   - `AnimChannel` — current frame (20.12) + length; `set_frame`, `advance`, with
     the exact `Anim_SetFrameWrapped` wrap. (First slice; see game_node.h.)
   - `NodeTransform` — translation, scale, Z-rotation, alpha.
   - `Node` — transform + a cell list + children; a `render(Renderer&)` that
     composes the transform down the tree and draws each cell affine.
   - An animation binds channel frames → transform/cell (the data comes from the
     game's NANR/animation tracks, decoded like other assets).
3. **Scenes build node trees** instead of hand-placing sprites: the title builds
   the logo/menu node tree, seeds the channels on entry (mirroring
   `func_ov000_0204e270`), and advances them per frame.

## Incremental plan

1. **AnimChannel model + test** — the frame/wrap core (this commit).
2. `NodeTransform` + `Node` tree + a neutral render walk (compose transforms).
3. `Renderer::draw_image_affine` in both backends (+ a software test).
4. Wire the title's menu/logo into a node tree; seed/advance channels 0 and 2
   from the decompiled entry; retire the hand-written eases as the tree subsumes
   them (page scroll and the selection pulse become channel/transform driven).
5. Reuse for ov008 (the in-game menu shares this system), once GameState exists.

Each step is self-contained and testable; the subsystem lands piece by piece
without a flag-day rewrite.
