# Xion model research tools

Read a locally supplied Kingdom Hearts 358/2 Days ROM and export Xion's models
as glTF, OBJ and PNG previews. No game data is included or downloaded.
These are rest-pose research exports, not an animation converter.

Requires Python 3.10+, NumPy and Pillow. From the repository root:

```sh
python tools/research/xion/xion_extract.py --rom /path/to/days.nds
python tools/research/xion/verify_exports.py
python tools/research/tests/xion_test.py
python tools/research/xion/census.py /path/to/days.nds /path/outside/the/repo/census.tsv
```

Exports go to the ignored `private/xion/` directory at the repository root.
`--out` selects another directory; keep those outputs private too. Do not attach
ROMs, models, textures or generated reports to a pull request.

The model list was checked against the European YKGP revision 0 cartridge.
`ba/ch/xo/def_h.p.z` is the high-detail, hood-down Xion; `def_hh.p.z` is its
hood-up counterpart. `mi/ch/0D.z` is a separate event model and rig. Other
cartridge revisions are not verified.

The Python model decoder follows `assets/src/mesh.cpp`, and texture decoding
follows `port/gpu/texdecode.h`, at khdays-port commit `0b65fa4f`. That code is
MIT licensed, copyright 2026 Yokimitsuro and contributors; the repository's
LICENSE applies to these tools. KAPH section 7 contains BMD0; section 0 holds
the joint animation. The source data's matrices use column-major storage.

Synthetic tests cover matrix serialization, noncommuting parent/child
transforms, texture alpha, and rejection of multiple palettes for one texture. The verifier
checks exported skin transforms and OBJ/glTF geometry counts. It is not a
complete glTF validator. Previews are diagnostic and do not reproduce all DS
material effects. Reusing a texture with different palette bindings is rejected
rather than silently exporting the first palette.
