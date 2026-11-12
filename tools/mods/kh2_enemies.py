"""KINGDOM HEARTS II's enemies for Days's custom missions, from the player's own 1.5+2.5 copy (docs/KH2_ENEMIES.md):
each kind's files as KH2 has them -- its model, script and collision (obj/<name>.mdlx), its motions (.mset), its
effects and sounds (.a.us), the model's HD pictures -- the shapes of KH2's calls read from its executable, and the
list of kinds with which of them the port covers (every call its script makes answered), which the port reads
(port/runtime/kh2_enemies/shell.h).

    python tools/mods/kh2_enemies.py --kh2 <kh2.hed_out> --kh2-exe <KINGDOM HEARTS II FINAL MIX.exe>
                                     [--exe <khdays-native>] [--mod "Days - KH2 Enemies"]

--kh2 is KH2's entries as the installer extracts them: original/obj/<name>.mdlx, .mset (the one its object entry in
original/00objentry.bin names: Hades of the Coliseum's cups moves by B_HE030.mset) and .a.us, a boss's battle voice bank
original/voice/us/battle/<bank>.win32.scd (its sounds to mods/<mod>/kh2/voice/<name>/<number>.wav), the HD pages
remastered/obj/<name>.mdlx/-N.dds (its model's) and remastered/obj/<name>.a.us/-N.dds (its effects'), and KH2's
resident effect pictures original/effect/texcommon.dpd with remastered/effect/texcommon.dpd/-N.dds; the sounds' banks
are each kind's own (remastered/obj/<name>.a.us/se/obj/se<bank>) and KH2's common one (original/se/se000.win32.scd).
--exe gives the calls the port answers (khdays-native --kh2-ai-calls); without it no kind is covered. Writes
mods/<mod>/kh2_enemies.txt, mods/<mod>/kh2/calls.txt, mods/<mod>/kh2/obj/ (each model's HD pages in kh2/obj/<name>/,
where the port's model reader looks), mods/<mod>/kh2/effect/texcommon.dpd, the effects' HD pages in
mods/<mod>/kh2/fx/<name>/ and kh2/fx/texcommon/, and the sounds the kinds play in mods/<mod>/kh2/se/<id>.wav (KH2's
sound id: bank x 1000 + its number). For the enemies' reaction commands, KH2's player's skeleton
(original/obj/P_EX100.mdlx, the bones the motions they make the player play are made for), his keyblade
(original/obj/W_EX010.mdlx, the bones his weapon's motions are made for, and W_EX010.a.us, its swing's trail, with
its HD pages in mods/<mod>/kh2/fx/W_EX010/), KH2's commands and the bone a player holds his weapon on
(original/03system.bin), its attacks' power (original/00battle.bin), the commands' names
(original/msg/<language>/sys.bar) and the journal's (msg/<language>/jm.bar: each kind's name, its title's number in
original/menu/us/jiminy.bar put in its line) go to mods/<mod>/kh2/ as KH2 lays them out, and KH2's general command
menu, whose
bar the reaction commands show (original/field2d/<language>/zz0command.2dd and its HD picture
remastered/field2d/<language>/zz0command.2dd/<LANGUAGE>_zz0command_2dd0.png) and the marks over its targets
(zz0field.2dd and its first HD picture: a reaction command's mark over the enemy) to mods/<mod>/kh2/field2d/<language>/,
and the font its names are written in (KH2's "sys": original/msg/us/fontinfo.bar, its glyphs' widths, and the HD
picture remastered/msg/us/fontimage.bar/US_fontimage_bar0.png) to mods/<mod>/kh2/msg/us/.
Says what it made and what it left out.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import kh2_bdx  # noqa: E402
import kh2_scd  # noqa: E402

# The kinds the mod holds and the names authors see: KH2's own data has no object's enemy name (docs/KH2_ENEMIES.md,
# "Names"), so each is named here (as OpenKH's object list names them). The Soldier and the Shadow first (US1); more as
# the port answers their calls (US2, docs/KH2_ENEMIES.md "Coverage").
KINDS = {
    "M_EX010": "Soldier",
    "M_EX020": "Shadow",
    "M_AL020_FIRE": "Fiery Globe",
    "M_AL020_ICEE": "Icy Cube",
    "M_EX510": "Rabid Dog",
    "M_EX610": "Strafer",
    "M_EX130": "Crimson Jazz",
    "M_EX200": "Wight Knight",
    "M_EX420": "Neoshadow",
    "M_EX560": "Hammer Frame",
    "M_EX570": "Assault Rider",
    "M_EX580": "Nightwalker",
    "M_EX600": "Magnum Loader",
    "M_EX640": "Hot Rod",
    "M_EX650": "Cannon Gun",
    "M_EX660": "Rapid Thruster",
    "M_EX760": "Armored Knight",
    "M_EX880": "Creeper",
    "M_EX990": "Dusk",
    "M_EX110": "Silver Rock",
    "M_EX520": "Hook Bat",
    "M_EX540": "Aeroplane",
    "M_EX630": "Luna Bandit",
    "M_EX700": "Mole Driller",
    "M_EX780": "Aerial Knocker",
    "M_EX790": "Graveyard",
    "M_EX930": "Dancer",
    "M_EX960": "Sorcerer",
    "B_HE020": "Cerberus",
    "B_BB100": "Thresholder",
    "B_BB130": "Possessor",
    "M_EX870": "Sorcerer's cubes",
    "B_HE030_CLSM": "Hades",
    "B_AL100_FIRE": "Volcanic Lord",
    "B_AL100_ICE": "Blizzard Lord",
    "B_AL110": "Volcanic Lord's lava",
    "B_AL120": "Blizzard Lord's ice spikes",
    "B_BB110": "Dark Thorn",
    "B_LK110": "Scar",
    "B_LK110_PHANTOM": "Scar's Ghost",
    "B_EX130": "Xaldin",
    "B_NM100": "Prison Keeper",
    "N_NM050_BTL_2": "Lock (in the Prison Keeper)",
    "N_NM060_BTL_2": "Shock (in the Prison Keeper)",
    "N_NM070_BTL_2": "Barrel (in the Prison Keeper)",
}
# The battle voice bank each kind speaks from (original/voice/us/battle/<bank>.win32.scd; 1:262 plays a sound of it by
# its number). KH2 gives an object its bank through its area's loading, which is not read: each is named here by the
# bank's name (Hades of the Coliseum's cups speaks as B_HE030's Hades, he_hades).
VOICES = {
    "B_HE030_CLSM": "he_hades",
    "B_LK110": "scar",
    "B_EX130": "n_dragoon",
    "N_NM050_BTL_2": "lock_boss",
    "N_NM060_BTL_2": "shock_boss",
    "N_NM070_BTL_2": "barrel_boss",
}
FILES = (".mdlx", ".mset", ".a.us")  # each kind's, as obj/ holds them (its .mset the one its object entry names)
# KH2's files the reaction commands read (under original/), each with what it is for
SHARED = (
    ("obj/P_EX100.mdlx", "the player's skeleton, which the enemies' motions for the player are made for"),
    ("obj/W_EX010.mdlx", "the player's keyblade's skeleton, which the enemies' motions for his weapon are made for"),
    ("obj/W_EX010.a.us", "the player's keyblade's effects: its swing's trail in a reaction command"),
    ("03system.bin", "KH2's commands: what a reaction command sends and the one it follows; the weapons' bones"),
    ("00battle.bin", "KH2's attacks' power: the player's blows in a reaction command"),
    ("00objentry.bin", "KH2's objects' entries: the numbers scripts find objects by (1:114, 1:222)"),
)
LANGUAGES = ("us", "fr", "gr", "it", "sp")  # msg/<language>/: sys.bar the commands' names, jm.bar the enemies'
JOURNAL = "jiminy.bar"  # menu/us/: KH2's journal, its characters' titles by object (the enemies' names: jm.bar's)
COMMAND_MENU = "zz0command.2dd"  # field2d/<language>/: KH2's general command menu (the reaction command's bar)
FIELD_MARKS = "zz0field.2dd"  # field2d/<language>/: KH2's marks over its targets ("fiel": a reaction command's)
# msg/us/: KH2's "sys" font, the bar's names' (its glyphs' widths; its HD picture under the remastered fontimage.bar)
FONT = ("fontinfo.bar", "fontimage.bar/US_fontimage_bar0.png")
COMMON_BANK = "se/se000.win32.scd"  # KH2's bank 0 (a sound id below 1000): the sounds all enemies share
PAX_ENTRY = 0x12  # an .a.us's PAX
ELEMENT = 0x50  # a PAX's elements, from its + 0x10 (their count at + 8): each one's sound at + 0x18 (none: 0 or -1)
SOUND_TRIGGER = 8  # a motion's single-frame trigger: a sound, (bank, number, ...)


def run_exe(line: list[str]) -> str:
    """khdays-native's run: its output; RuntimeError with what it said when it fails."""
    done = subprocess.run(line, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != 0:
        said = done.stderr.strip().splitlines()
        raise RuntimeError(said[-1] if said else f"exit {done.returncode}")
    return done.stdout


def calls_in(bar: bytes) -> set[tuple[int, int]]:
    """Every call a BAR's scripts make, their state routines walked."""
    return {i.syscall for _, s in kh2_bdx.scripts_in(bar) for i in kh2_bdx.walk(s).values() if i.syscall is not None}


def kinds_line(
    name: str, title: str, calls: set[tuple[int, int]], handled: set[tuple[int, int]], journal: int | None = None
) -> str:
    """A kind's line of kh2_enemies.txt: name, title, covered, how many calls, the missing ones, "player" when its
    script plays the player's motions itself (1:29: its reaction commands the port can play) else "-", and its name's
    text in KH2's journal messages (msg/<language>/jm.bar; journal_title) else "-" (tab-separated)."""
    missing = sorted(calls - handled)
    listed = " ".join(f"{k}:{n}" for k, n in missing) or "-"
    plays = "player" if (1, 29) in calls else "-"
    named = str(journal) if journal is not None else "-"
    return f"{name}\t{title}\t{'no' if missing or not calls else 'yes'}\t{len(calls)}\t{listed}\t{plays}\t{named}"


def journal_titles(jiminy: bytes, objentry: bytes) -> dict[str, int]:
    """KH2's journal's characters -- its Heartless, Nobodies and bosses among them -- by their object's model name:
    each one's title text (menu/<language>/jiminy.bar's JMCH, OpenKH's jiminy page: a 16-byte header, its count at
    + 8, then 36-byte records, + 6 the title, + 0xc the object's number; 00objentry.bin gives the number's model, a u32
    count at 4, then 96-byte records from 8, the number at + 0, the model's name at + 8). The first of a model's."""
    models = {}
    count = struct.unpack_from("<I", objentry, 4)[0] if len(objentry) >= 8 else 0
    for i in range(count):
        at = 8 + 96 * i
        if at + 96 > len(objentry):
            break
        models.setdefault(struct.unpack_from("<H", objentry, at)[0], objentry[at + 8 : at + 0x28].split(b"\0")[0])
    out: dict[str, int] = {}
    for _, at, size in bar_entries(jiminy):
        if size < 16 or jiminy[at : at + 4] != b"JMCH":
            continue
        records = struct.unpack_from("<I", jiminy, at + 8)[0]
        for k in range(records):
            r = at + 16 + 36 * k
            if r + 36 > at + size:
                break
            title, number = struct.unpack_from("<H", jiminy, r + 6)[0], struct.unpack_from("<H", jiminy, r + 0xC)[0]
            model = models.get(number)
            if model:
                out.setdefault(model.decode("latin-1").upper(), title)
    return out


def journal_title(titles: dict[str, int], name: str) -> int | None:
    """A kind's title in the journal: its own model's, else its journal's (_GM: the Volcanic Lord's B_AL100_FIRE is
    the journal's B_AL100_FIRE_GM), else its family's -- the model's first two parts (Hades of the cups' B_HE030_CLSM
    is the journal's B_HE030_GM) --, else the journal's first model named on from it (the Wight Knight's M_EX200 is
    the journal's M_EX200_NM); none when the journal has none."""
    family = "_".join(name.upper().split("_")[:2])
    for candidate in (name.upper(), f"{name.upper()}_GM", f"{family}_GM", family):
        if candidate in titles:
            return titles[candidate]
    return next((title for model, title in titles.items() if model.startswith(f"{name.upper()}_")), None)


def copy_pages(source: Path, to: Path) -> int:
    """An entry's HD pages (-N.dds) into `to`: how many."""
    if not source.is_dir():
        return 0
    to.mkdir(parents=True, exist_ok=True)
    pages = sorted(source.glob("-*.dds"))
    for page in pages:
        shutil.copy2(page, to / page.name)
    return len(pages)


def bar_entries(data: bytes, base: int = 0) -> list[tuple[int, int, int]]:
    """A BAR's entries at `base` as (type, offset, size), the offsets from the file's start; none if it is no BAR."""
    if data[base : base + 4] != kh2_bdx.BAR:
        return []
    out = []
    for k in range(struct.unpack_from("<I", data, base + 4)[0]):
        at = base + 16 + 16 * k
        if at + 16 > len(data):
            break
        kind, _, _, offset, size = struct.unpack_from("<HH4sII", data, at)
        out.append((kind, base + offset, size))
    return out


def motion_sounds(mset: bytes) -> set[int]:
    """The sounds a motion set's triggers 8 play (bank x 1000 + number). Each motion a BAR; its attributes (type
    0x10): u8 ranges, u8 triggers, u16 the triggers' offset; a range u16 start, u16 end, u8 id, u8 n, n u16; a
    trigger u16 frame, u8 id, u8 n, n u16 (as assets/src/kh2_motion.cpp reads them). A slot of type 0x14 holds the
    motions the set's object makes another play (1:29: the player's in a reaction command), each part a motion BAR of
    its own ('0010' his body, '0011' his weapon: the keyblade's bank 500)."""
    out = set()
    motions = []
    for kind, at, size in bar_entries(mset):
        if not size:
            continue
        if (kind & 0xFF) == 0x14:
            motions += [a for _, a, s in bar_entries(mset, at) if s]
        else:
            motions.append(at)
    for at in motions:
        for kind, a, s in bar_entries(mset, at):
            end = a + s
            if kind != 0x10 or s < 4 or end > len(mset):
                continue
            ranges, triggers, offset = struct.unpack_from("<BBH", mset, a)
            p = a + 4
            for _ in range(ranges):
                p += 6 + 2 * mset[p + 5] if p + 6 <= end else 0
            if offset:
                p = a + offset
            for _ in range(triggers):
                if p + 4 > end or p + 4 + 2 * mset[p + 3] > end:
                    break
                params = struct.unpack_from(f"<{mset[p + 3]}h", mset, p + 4)
                if mset[p + 2] == SOUND_TRIGGER and len(params) >= 2 and params[0] >= 0 and params[1] >= 0:
                    out.add(params[0] * 1000 + params[1])
                p += 4 + 2 * mset[p + 3]
    return out


def effect_sounds(a_us: bytes) -> set[int]:
    """The sounds an .a.us's effects' elements play with them (each one's + 0x18 above 0)."""
    for kind, at, size in bar_entries(a_us):
        if kind == PAX_ENTRY and a_us[at : at + 4] == b"PAX_" and at + 0x10 <= len(a_us):
            count = struct.unpack_from("<I", a_us, at + 8)[0]
            places = [at + 0x10 + ELEMENT * k for k in range(count) if 0x10 + ELEMENT * (k + 1) <= size]
            return {s for p in places if (s := struct.unpack_from("<i", a_us, p + 0x18)[0]) > 0}
    return set()


def make_sounds(kh2: Path, name: str, ids: set[int], to: Path) -> list[str]:
    """Each sound in `ids` into to/<id>.wav, its first track's recording at the sound's volume, from its bank: bank 0
    KH2's common one, the others the kind's own (its .a.us's remastered se/obj/se<bank>), else another object's that
    carries it (the Sorcerer's cubes play bank 272, which the Sorcerer's .a.us carries); what was left out."""
    left_out = []
    banks: dict[int, tuple[list, list] | str] = {}
    for sound_id in sorted(ids):
        bank, number = divmod(sound_id, 1000)
        path = (
            kh2 / "original" / COMMON_BANK
            if bank == 0
            else kh2 / "remastered" / "obj" / f"{name}.a.us" / "se" / "obj" / f"se{bank}"
        )
        if bank != 0 and not path.is_file():
            path = next(iter(sorted((kh2 / "remastered" / "obj").glob(f"*.a.us/se/obj/se{bank}"))), path)
        if bank not in banks:
            try:
                data = path.read_bytes()
                banks[bank] = (kh2_scd.sounds(data), kh2_scd.recordings(data))
            except (OSError, kh2_scd.ScdError) as e:
                banks[bank] = str(e)
        read = banks[bank]
        if isinstance(read, str):
            left_out.append(f"{name}'s sound {sound_id}: its bank {path.name} is not read ({read})")
            continue
        sounds, recordings = read
        sound = sounds[number] if number < len(sounds) else None
        recording = recordings[sound.recordings[0]] if sound is not None and sound.recordings else None
        if recording is None:
            left_out.append(f"{name}'s sound {sound_id}: {path.name} has no recording for it")
            continue
        try:
            samples = kh2_scd.decode(recording).astype(np.float64) * sound.volume
        except kh2_scd.ScdError as e:
            left_out.append(f"{name}'s sound {sound_id}: {e}")
            continue
        scaled = np.clip(np.rint(samples), -32768, 32767).astype(np.int16)
        kh2_scd.write_wav(to / f"{sound_id}.wav", scaled, recording.rate)
    return left_out


def motion_set_of(objentry: bytes, name: str) -> str:
    """The motion set a kind's object entry names (00objentry.bin: a u32 count at 4, then 96-byte records from 8, the
    model's name at + 8 and the motion set's at + 0x28, 32 bytes each; OpenKH's 00objentry page): Hades of the
    Coliseum's cups (B_HE030_CLSM) moves by B_HE030.mset. <name>.mset when no entry of that model names one."""
    count = struct.unpack_from("<I", objentry, 4)[0] if len(objentry) >= 8 else 0
    for i in range(count):
        at = 8 + 96 * i
        if at + 96 > len(objentry):
            break
        model = objentry[at + 8 : at + 0x28].split(b"\0")[0].decode("latin-1")
        motions = objentry[at + 0x28 : at + 0x48].split(b"\0")[0].decode("latin-1")
        if model == name and motions:
            return motions
    return f"{name}.mset"


def make_voices(kh2: Path, name: str, bank: str | None, to: Path) -> list[str]:
    """The kind's voice bank's sounds (VOICES) by number, as 1:262 names them: its first track's recording -- Ogg
    Vorbis (the battle banks') into to/<number>.ogg as it is, its sound's volume in to/volumes.txt ("<number>
    <volume>" a line), Microsoft ADPCM into to/<number>.wav at the volume; what was left out."""
    if bank is None:
        return []
    path = kh2 / "original" / "voice" / "us" / "battle" / f"{bank}.win32.scd"
    try:
        data = path.read_bytes()
        sounds, recordings = kh2_scd.sounds(data), kh2_scd.recordings(data)
    except (OSError, kh2_scd.ScdError) as e:
        return [f"{name}'s voice: {path.name} is not read ({e})"]
    to.mkdir(parents=True, exist_ok=True)
    left_out, volumes = [], []
    for number, sound in enumerate(sounds):
        recording = recordings[sound.recordings[0]] if sound is not None and sound.recordings else None
        if recording is None:
            continue
        try:
            if recording.codec == kh2_scd.VORBIS:
                (to / f"{number}.ogg").write_bytes(kh2_scd.vorbis(recording))
                volumes.append(f"{number} {sound.volume:.6g}")
                continue
            samples = kh2_scd.decode(recording).astype(np.float64) * sound.volume
        except kh2_scd.ScdError as e:
            left_out.append(f"{name}'s voice {number}: {e}")
            continue
        scaled = np.clip(np.rint(samples), -32768, 32767).astype(np.int16)
        kh2_scd.write_wav(to / f"{number}.wav", scaled, recording.rate)
    if volumes:
        (to / "volumes.txt").write_text("\n".join(volumes) + "\n", encoding="utf-8", newline="\n")
    return left_out


def make(
    kh2: Path, kh2_exe: Path, mod: Path, exe: Path | None = None, run: Callable[[list[str]], str] = run_exe
) -> list[str]:
    """The calls' shapes, each kind's files and the list into mod/; what was left out, each with why."""
    left_out = []
    shapes = kh2_bdx.exe_calls(kh2_exe.read_bytes())
    calls_file = mod / "kh2" / "calls.txt"
    calls_file.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# KINGDOM HEARTS II's calls: kind:number arguments gives-back (read from its executable, kh2_enemies.py)"]
    lines += [f"{kind}:{number} {count} {int(back)}" for kind, number, count, back in shapes]
    calls_file.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    handled = kh2_bdx.read_handled(run([str(exe), "--kh2-ai-calls"])) if exe is not None else set()
    if exe is None:
        left_out.append("every kind's coverage: no khdays-native given (--exe), none is covered")
    obj = mod / "kh2" / "obj"
    obj.mkdir(parents=True, exist_ok=True)
    common = kh2 / "original" / "effect" / "texcommon.dpd"
    if common.is_file():  # the pictures KH2 keeps resident, which the effects may cut
        (mod / "kh2" / "effect").mkdir(parents=True, exist_ok=True)
        shutil.copy2(common, mod / "kh2" / "effect" / common.name)
        copy_pages(kh2 / "remastered" / "effect" / common.name, mod / "kh2" / "fx" / "texcommon")
    else:
        left_out.append("KH2's resident effect pictures (effect/texcommon.dpd): effects cutting them drawn without")
    listed = [
        "# name\ttitle\tcovered\tcalls\tmissing\tplays the player\tjournal name (kh2_enemies.py; the port's "
        "kh2_enemies.txt)"
    ]
    entries_file = kh2 / "original" / "00objentry.bin"
    entries = entries_file.read_bytes() if entries_file.is_file() else b""
    jiminy_file = kh2 / "original" / "menu" / "us" / JOURNAL
    titles = journal_titles(jiminy_file.read_bytes(), entries) if jiminy_file.is_file() else {}
    if not jiminy_file.is_file():
        left_out.append(f"menu/us/{JOURNAL} (the enemies' names): not there; their stand-ins' names show")
    for name, title in KINDS.items():
        sources = [kh2 / "original" / "obj" / f"{name}{ext}" for ext in FILES]
        sources[1] = sources[1].with_name(motion_set_of(entries, name))
        if not sources[0].is_file():
            left_out.append(f"{name} ({title}): KH2's {sources[0].name} not there")
            continue
        # an object with no model and no motion set of its own (its entry names none: the Thresholder's
        # Possessor) has its .mdlx alone, its script and its shapes; its effects when it has them
        model = any(kind == 4 for kind, _, _ in bar_entries(sources[0].read_bytes()))
        if model and not sources[1].is_file():
            left_out.append(f"{name} ({title}): KH2's {sources[1].name} not there")
            continue
        for s in sources:
            if s.is_file():
                shutil.copy2(s, obj / s.name)
        if model and not copy_pages(kh2 / "remastered" / "obj" / f"{name}.mdlx", obj / name):
            left_out.append(f"{name} ({title}): its model's HD pictures (its own PS2 pictures used)")
        copy_pages(kh2 / "remastered" / "obj" / f"{name}.a.us", mod / "kh2" / "fx" / name)
        sounds = (motion_sounds(sources[1].read_bytes()) if sources[1].is_file() else set()) | (
            effect_sounds(sources[2].read_bytes()) if sources[2].is_file() else set()
        )
        left_out += make_sounds(kh2, name, sounds, mod / "kh2" / "se")
        left_out += make_voices(kh2, name, VOICES.get(name), mod / "kh2" / "voice" / name)
        calls = calls_in(sources[0].read_bytes())
        listed.append(kinds_line(name, title, calls, handled, journal_title(titles, name)))
        print(f"kh2 enemies: made {name} ({title}), {len(calls)} calls, {len(sounds)} sounds", file=sys.stderr)
    (mod / "kh2_enemies.txt").write_text("\n".join(listed) + "\n", encoding="utf-8", newline="\n")
    for part, what in SHARED:
        source = kh2 / "original" / part
        if source.is_file():
            (mod / "kh2" / part).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, mod / "kh2" / part)
        else:
            left_out.append(f"{part} ({what}): not there; the reaction commands go without it")
    if not copy_pages(kh2 / "remastered" / "obj" / "W_EX010.a.us", mod / "kh2" / "fx" / "W_EX010"):
        left_out.append("the keyblade's effects' HD pictures (remastered/obj/W_EX010.a.us): its trail in its PS2 pictures")
    names = journals = 0
    for language in LANGUAGES:
        for file in ("sys.bar", "jm.bar"):
            source = kh2 / "original" / "msg" / language / file
            if source.is_file():
                (mod / "kh2" / "msg" / language).mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, mod / "kh2" / "msg" / language / file)
                names += file == "sys.bar"
                journals += file == "jm.bar"
    if names == 0:
        left_out.append("msg/<language>/sys.bar (the commands' names): not there; a reaction command shows its button")
    if journals == 0:
        left_out.append("msg/<language>/jm.bar (the enemies' names): not there; their stand-ins' names show")
    menus = marks = 0
    for language in LANGUAGES:
        for name in (COMMAND_MENU, FIELD_MARKS):
            archive = kh2 / "original" / "field2d" / language / name
            stem = name.replace(".", "_")
            picture = kh2 / "remastered" / "field2d" / language / name / f"{language.upper()}_{stem}0.png"
            if archive.is_file() and picture.is_file():
                folder = mod / "kh2" / "field2d" / language
                folder.mkdir(parents=True, exist_ok=True)
                shutil.copy2(archive, folder / name)
                shutil.copy2(picture, folder / picture.name)
                menus += name == COMMAND_MENU
                marks += name == FIELD_MARKS
    if menus == 0:
        left_out.append(
            f"field2d/<language>/{COMMAND_MENU} and its HD picture (the reaction command's bar): not there; "
            "Days's deck keeps its own commands"
        )
    if marks == 0:
        left_out.append(
            f"field2d/<language>/{FIELD_MARKS} and its HD picture (the reaction command's mark): not there; the "
            "button's icon marks the enemy"
        )
    info = kh2 / "original" / "msg" / "us" / FONT[0]
    picture = kh2 / "remastered" / "msg" / "us" / FONT[1]
    if info.is_file() and picture.is_file():
        (mod / "kh2" / "msg" / "us").mkdir(parents=True, exist_ok=True)
        shutil.copy2(info, mod / "kh2" / "msg" / "us" / info.name)
        shutil.copy2(picture, mod / "kh2" / "msg" / "us" / picture.name)
    else:
        left_out.append(
            "msg/us/fontinfo.bar and fontimage.bar's HD picture (KH2's font): not there; the bar's names in Days's font"
        )
    return left_out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kh2", type=Path, required=True, help="KH2's entries extracted (<kh2.hed_out>)")
    ap.add_argument("--kh2-exe", type=Path, required=True, help="KINGDOM HEARTS II FINAL MIX.exe (its calls' shapes)")
    ap.add_argument("--exe", type=Path, help="khdays-native, for the calls the port answers (--kh2-ai-calls)")
    ap.add_argument("--mod", default="Days - KH2 Enemies", help="the mod folder under mods/")
    args = ap.parse_args(argv)
    try:
        left_out = make(args.kh2, args.kh2_exe, Path("mods") / args.mod, args.exe)
    except (OSError, RuntimeError, kh2_bdx.AiError) as e:
        print(f"kh2 enemies: {e}", file=sys.stderr)
        return 1
    for line in left_out:
        print(f"kh2 enemies: left out {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
