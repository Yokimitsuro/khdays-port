# KH2 models (.mdlx)

KINGDOM HEARTS II's character and object models (`obj/<name>.mdlx` in the
player's 1.5+2.5 copy), drawn by the port instead of a Days model. Read by
`assets/src/kh2_mdlx.cpp` (`kh2::import_mdlx`) on top of the KH2 map readers in
`assets/src/kh2_map.cpp` (BAR, object models, texture containers), and drawn
like any mod's model (`RENDERING.md`, "Mods' graphics"). Nothing of the game is
in the repository; the file comes from the player.

Stage 1 (this page): the model at rest (a T-pose) with its textures, on Days'
node it is paired with. Animations, maps and collision come later.

## Using one

```text
mods/<Mod>/graphics/<file>[#<entry>]/mdl/<model>.mdlx
mods/<Mod>/graphics/<file>[#<entry>]/mdl/<model>.bones.txt
```

`<file>/mdl/<model>` is the Days model's name as `--dump-graphics` writes it.
`.mdlx` is tried after `.gltf`, `.glb` and `.fbx`. The joints are the KH2 bones,
named `kh2_<bone index>`; `<model>.bones.txt` pairs them with Days' nodes as
for any mod model (`kh2_0 roxas_R`). Bones without a pair follow their parent.
KH2's units are centimetres: the importer makes them metres (x 0.01), the frame
mods' models use, so a KH2 character comes out at a Days character's size
(Days' Roxas is 1.60 tall in the dump, KH2's 1.69) without a `scale` line.

## The file

Little-endian on the PC (the HD collections); the PS3 build in IDA is
big-endian. A BAR (`kh2_bar.h`): entry type 0x04 the model, 0x07 its textures,
others not read (0x17 seen). The model's header follows 0x90 bytes the game
keeps for itself.

**Model** (`kn::ModelSKLRAW`, PS3 `model_sklWIN.cpp`): u32 kind (3: an object
or character), u32 at +0xc the offset of a chained model (a shadow model; not
read), u16 bone count +0x10, u16 texture count +0x12, u32 bone table +0x14,
u32 +0x18, u32 subpart count +0x1c, then the subparts (0x20 bytes each,
`kn::ModelSKLRAWGroup`): u32 attribute, u32 texture (GS info index), u32
polygon count, u16 hasVB, u16 alt, u32 DMA chain offset, u32 matrix info
offset, u32 packet size, u32 reserved. Bones are 0x40 bytes: u32 index, i32
parent, scale, rotation (radians), translation (16 bytes each); a bone's matrix
is its parent's x T x Rz x Ry x Rx x S.

**Attribute** (`kn::ModelSKLRAWAttribute`, LE bit order on the PC): bit 0 draw
alpha phase, 1 translucent, 2 multi, 6-10 part, 11-15 mesh, 16-20 priority,
21 alphaEx, 22-26 UV scroll, 27 additive, 28 subtractive, 29 specular, 30 no
light. Translucent subparts are drawn blended, the others alpha-tested.

**Packets.** A subpart's DMA chain: ref tags of 4 quadwords bring a bone's
matrix (the tag's address is the bone; its UNPACK's address the VU slot), other
ref tags data from the model, a cnt tag inline data, and closes a packet. The
packet's VIF UNPACKs fill VU memory; its header, u32 words h[0..]
(`kn::SKLPOLYGONDATA::SKLPOLYGONDATA`, 0x6ac7b8):

| Word | Meaning |
|---|---|
| h0 | 1 model, 2 shadow |
| h4, h5 | strip vertex count, list address (u, v as s16 / 4096; index; flag) |
| h6 | matrix affiliation list: how many positions go with each matrix |
| h8, h9 | colour count, address (V4-8, 0x80 = 1.0) |
| h10, h11 | weight group count (0-7), address of their counts |
| h12, h13 | position count, address (x, y, z, w) |
| h14 | blended index list address |
| h15 | matrix count |
| h16, h17 | normal count, address (a 5-quadword header) |

A strip flag ends a triangle as in the map models (`kh2_map.cpp`).

**Blended vertices** (`setVertexWithWeight` in `SKLPOLYGONDATA::setVertex`,
0x6ad418). With h10 > 0 the strip indexes blended vertices, not positions.
Weight group g (0-based) holds count[g] vertices of g + 1 positions each; the
group's position indices follow on from h14, each group's list starting on a
quadword. A position's w is its weight (> 0, asserted) and its x y z are
already scaled by it: the vertex is the sum of matrix x (x, y, z, w) over its
positions. Each position's matrix is the one whose affiliation range holds the
position's index. Verified on P_EX110: every vertex's weights add to 1.0 and
the mesh closes up at rest. The importer keeps a vertex's four heaviest bones
(at most two seen).

**Textures** (`kn::Texture`, `kn::convertTexture` 0x110dc8, `kn::makeTexture`
0x11055c): u16 type, u16 flag, u32 colour count, picture count, GS info count,
then the offsets of the pictures' numbers, the send packets (0x90 bytes each,
the palette's first; BITBLTBUF at +0x20, TRXPOS +0x30, TRXREG +0x40, the
image's address at +0x74), the GS infos (0xA0 bytes; TEX0 at +112, CLAMP at
+128), the pixels and the palette. The importer uploads each packet's picture
into a model of the GS's memory as the GS would (PSMCT32 or PSMT8 transfers)
and reads every texture back in its own format: P_EX110's 8-bit textures are
sent as 32-bit pixels at half the width, so the read-back undoes the GS's
swizzle. The palette is read as a 16 x 16 PSMCT32 rectangle at CBP with CSM1's
order (entries 8-15 and 16-23 of each 32 swapped); `kn::clt2vram` is the PS3's
equivalent. PS2 alpha (0x80 = opaque) is doubled. The whole TW x TH texture is
kept, texture coordinates over it as stored.

## Not yet

- 4-bit textures (PSMT4) and other formats: reported and drawn white.
- Region clamp is drawn as repeat (the renderer repeats mods' textures).
- Normals, UV scroll, the chained shadow model and entry 0x17 are not read.
- The remastered HD textures (`remastered/obj/<name>.mdlx/-N.dds`) are not used.
