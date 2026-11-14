"""KINGDOM HEARTS II's enemies for Days's custom missions, from the player's own 1.5+2.5 copy (docs/KH2_ENEMIES.md):
each kind's files as KH2 has them -- its model, script and collision (obj/<name>.mdlx), its motions (.mset), its
effects and sounds (.a.us), the model's HD pictures -- the shapes of KH2's calls read from its executable, and the
list of kinds with which of them the port covers (every call its script makes answered), which the port reads
(port/runtime/kh2_enemies/shell.h).

    python tools/mods/kh2_enemies.py --kh2 <kh2.hed_out> --kh2-exe <KINGDOM HEARTS II FINAL MIX.exe>
                                     [--exe <khdays-native>] [--mod "Days - KH2 Enemies"] [--only NAME ...]

--only makes the objects named alone, with the files every kind needs, into a mod of one's own that carries the
enemies its missions use (the port reads every mod's kh2_enemies.txt); what they make or bring and was not named is
told, not added.

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
import math
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
    "B_HE100": "Hydra",
    "B_HE110": "Hydra's head",
    "N_HE020_BTL": "Phil (with the Hydra)",
    "N_HE030_BTL": "Megara (with the Hydra)",
    "N_HE040_BTL": "Pegasus (with the Hydra)",
    "F_HE030_L": "Urn (Phil's, with the Hydra)",
    "B_EX170": "Xemnas",
    "B_EX120": "Demyx",
    "M_EX880_DANCER": "Water clone (Demyx's)",
    "B_CA020": "Undead Pirate A",
    "B_CA030": "Undead Pirate B",
    "B_CA040": "Undead Pirate C",
    "B_CA010": "Captain Barbossa",
    "B_CA000": "Illuminator",
    "B_EX110": "Axel",
    "B_LK100_00": "Shenzi",
    "B_LK100_10": "Banzai",
    "B_LK100_20": "Ed",
    "B_BB120": "Shadow Stalker",
    "B_NM000": "Oogie Boogie",
    # FINAL MIX's data battles (their Limit Cut objects, _LV99)
    "B_EX110_LV99": "Axel (Data)",
    "B_EX130_LV99": "Xaldin (Data)",
    "B_EX170_LV99": "Xemnas (Data)",
    "B_EX120_HB_LV99": "Demyx (Data)",
    "M_EX880_DANCER_LV99": "Water clone (Data Demyx's)",
    "B_EX160_LV99": "Saïx (Data)",
    "B_EX220_LV99": "Saïx's claymore (Data)",
    # his FINAL MIX fight and his data battle are one object (eh21's b_40 and b_80, the latter at battle level 99)
    "B_EX390": "Roxas",
    "W_EX010_ROXAS_LIGHT": "Roxas's keyblade (light)",
    "W_EX010_ROXAS_DARK": "Roxas's keyblade (dark)",
    # her Absent Silhouette and her data battle (hb33's b_83 and b_81, battle level 32 and 99)
    "B_EX400": "Larxene",
    # FINAL MIX's Lingering Will
    "B_EX420": "Lingering Will",
    # his Absent Silhouette and his data battle (hb34's b_80 and b_81), and what his script makes: his books (their
    # meteors, storms, tornados, shields) and his illusion (it takes the player away: Breakout, Dispel)
    "B_EX370": "Zexion",
    "B_EX380": "Zexion's book",
    "B_EX410": "Zexion's illusion",
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
    "B_EX170": "n_xemnas",
    "B_EX120": "n_dancer",
    "B_CA010": "barbossa",
    "B_EX110": "n_axel",
    "B_LK100_20": "ed",
    "B_NM000": "boogie",
    "B_EX110_LV99": "n_axel",
    "B_EX130_LV99": "n_dragoon",
    "B_EX170_LV99": "n_xemnas",
    "B_EX120_HB_LV99": "n_dancer_hb",
    "B_EX160_LV99": "n_berserker",
    "B_EX390": "n_roxas",
    "B_EX400": "n_larxene",
    "B_EX370": "n_zexion",
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
AREAS = "ard/us"  # original/<AREAS>/*.ard: KH2's areas, whose spawns place a boss's companions
SPAWN_ENTRY = 0xC  # an .ard's spawn file (OpenKH's ARD page, "Spawn Point")
PLACED_SETS = (1, 2)  # the spawn sets KH2 places as its area loads (type 5: those its scripts make, 1:0)
PARTY = (0, 1)  # object entries' kinds of the player and his party (PLAYER, FRIEND_1, FRIEND_2)
SCRIPT_ENTRY = 0xD  # an .ard's scripts (map, btl, evt)
SPAWN_OP, MISSION_OP = 0x0, 0x15  # their operations: a spawn file placed, a mission run (OpenKH's ARD page)
MISSIONS = "msn/us"  # original/<MISSIONS>/<name>.bar: KH2's missions (OpenKH's MSN page)
BOSSES = (3, 0xC)  # object entries' kinds of a boss and a large enemy (the Hydra)
# Bosses bound to their KH2 rooms (room_objects: Shadow Stalker, Dark Thorn, Oogie Boogie) left out for now (the
# maintainer, 2026-11-13): a room made from their own map still lacks what KH2's does for them (its conveyors, its
# objects' own collision). True makes them, their room's objects and what their scripts make, as the port can play
# them (docs/KH2_ENEMIES.md, "Bosses bound to their rooms").
ROOM_BOSSES = False
ENEMIES = (3, 4, 0xC)  # and of an enemy: the port makes each a Days enemy


def run_exe(line: list[str]) -> str:
    """khdays-native's run: its output; RuntimeError with what it said when it fails."""
    done = subprocess.run(line, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != 0:
        said = done.stderr.strip().splitlines()
        raise RuntimeError(said[-1] if said else f"exit {done.returncode}")
    return done.stdout


def searched_numbers(bar: bytes) -> set[int]:
    """The object entries' numbers a BAR's scripts find objects by (a number pushed just before 1:114,
    trap_obj_search_by_entry), but the player's (0x236): Shadow Stalker's pillars and floor, Oogie Boogie's machinery."""
    out = set()
    for _, s in kh2_bdx.scripts_in(bar):
        code = kh2_bdx.walk(s)
        order = sorted(code)
        for before, at in zip(order, order[1:]):
            pushed = code[before].pushed
            if code[at].syscall == (1, 114) and pushed is not None and pushed != 0x236 and before + code[before].size == at:
                out.add(pushed)
    return out


def made_numbers(bar: bytes) -> set[int]:
    """The object entries' numbers a BAR's scripts make objects by (1:0, trap_sysobj_appear, its one argument): pushed
    right before it, or given to a routine that passes it on (kh2_bdx.argument_values: Oogie Boogie's gift boxes, 956
    and 957, through one of his)."""
    out = set()
    for _, s in kh2_bdx.scripts_in(bar):
        out |= kh2_bdx.argument_values(s, kh2_bdx.walk(s), (1, 0), 1, 0)
    return out


def room_objects(
    named_ards: list[tuple[str, bytes]], entries: dict[int, tuple[str, int]], name: str, searched: set[int]
) -> tuple[str | None, list[tuple[str, float, float, float, float, int]]]:
    """A boss bound to its room: the area of its battle (the first whose battle spawn, "b_..", places it), when its
    script finds one of the room's own objects by number (searched_numbers: KH2's field objects, named F_, but those
    the mod lists as kinds of their own, which a mission's author places) that a spawn of that area places as it loads
    (its map's "m_.." ones too: the ballroom's pillars); and the room's objects then: those, and every field object of
    an enemy's kind the area's spawns place (the room's battle pieces, which the boss reaches by a shouted state:
    Shadow Stalker's chandelier, F_BB050, "down_shock_hyoui"), each once, as (name, x, y, z, yaw, its argument): KH2's
    own place on the area's map, in KH2's world axes, and its spawn's first argument (spawn_sets). None and none when
    its script finds no such object there (a boss that fights anywhere)."""
    if not searched or next((kind for n, kind in entries.values() if n == name), -1) not in BOSSES:
        return None, []
    for area, ard in named_ards:
        arguments: dict[tuple[str, int, int], int] = {}
        sets_of = spawn_sets(ard, arguments=arguments)
        if not any(
            entry.startswith("b")
            and any(entries.get(o[0], ("", -1))[0] == name for kind, objects in sets if kind in PLACED_SETS for o in objects)
            for entry, sets in sets_of
        ):
            continue
        out, seen = [], set()
        for entry, sets in sets_of:
            for place, (kind, objects) in enumerate(sets):
                if kind not in PLACED_SETS:
                    continue
                for k, o in enumerate(objects):
                    other, its_kind = entries.get(o[0], ("", -1))
                    found = o[0] in searched or its_kind in ENEMIES
                    if found and o[0] not in seen and other.startswith("F_") and other not in KINDS:
                        seen.add(o[0])
                        out.append((other, o[1], o[2], o[3], o[4], arguments.get((entry, place, k), 0)))
        return (area, out) if searched & seen else (None, [])
    return None, []


def calls_in(bar: bytes) -> set[tuple[int, int]]:
    """Every call a BAR's scripts make, their state routines walked."""
    return {i.syscall for _, s in kh2_bdx.scripts_in(bar) for i in kh2_bdx.walk(s).values() if i.syscall is not None}


def kinds_line(
    name: str,
    title: str,
    calls: set[tuple[int, int]],
    handled: set[tuple[int, int]],
    journal: int | None = None,
    with_it: list[tuple[str, float, float, float, float]] | None = None,
    mission: str | None = None,
    paths: list[tuple[int, list[tuple[float, float, float]]]] | None = None,
    home: str | None = None,
    room: list[tuple[str, float, float, float, float, int]] | None = None,
    argument: int = 0,
) -> str:
    """A kind's line of kh2_enemies.txt: name, title, covered, how many calls, the missing ones, "player" when its
    script plays the player's motions itself (1:29: its reaction commands the port can play) else "-", its name's
    text in KH2's journal messages (msg/<language>/jm.bar; journal_title) else "-", and the objects KH2 places with it
    (companions: "name x y z yaw" each, apart by ";") else "-", and the mission KH2's area runs with its battle (its
    msn file's name: kh2/msn/<name>.bar) else "-", the walk paths of its battle (walk_paths: "number x y z x y z
    ..." each, apart by ";") else "-", the KH2 map it is bound to (room_objects: its battle's area, bb05) else "-" --
    the port makes it only in a room made from that map -- its room's objects there ("name x y z yaw argument" each,
    KH2's world, apart by ";") else "-", and the first argument its KH2 battle spawn gives it (unit_argument; 0 none)
    (tab-separated)."""
    missing = sorted(calls - handled)
    listed = " ".join(f"{k}:{n}" for k, n in missing) or "-"
    plays = "player" if (1, 29) in calls else "-"
    named = str(journal) if journal is not None else "-"
    placed = ";".join(f"{n} {x:.1f} {y:.1f} {z:.1f} {yaw:.3f}" for n, x, y, z, yaw in with_it or []) or "-"
    covered = "no" if missing or not calls else "yes"
    ways = ";".join(f"{n} " + " ".join(f"{x:.1f} {y:.1f} {z:.1f}" for x, y, z in points) for n, points in paths or [])
    pieces = ";".join(f"{n} {x:.1f} {y:.1f} {z:.1f} {yaw:.3f} {arg}" for n, x, y, z, yaw, arg in room or []) or "-"
    return (
        f"{name}\t{title}\t{covered}\t{len(calls)}\t{listed}\t{plays}\t{named}\t{placed}\t{mission or '-'}"
        f"\t{ways or '-'}\t{home or '-'}\t{pieces}\t{argument}"
    )


def entry_kinds(objentry: bytes) -> dict[int, tuple[str, int]]:
    """00objentry.bin's entries by number: (model name, kind) -- + 0 the number, + 4 the kind (0 the player, 1 his
    party, 3 a boss, 4 an enemy, 9 a friend in battle, 0xc a large enemy, 0x17 a gimmick; OpenKH's 00objentry page
    names some), + 8 the model's name."""
    out = {}
    count = struct.unpack_from("<I", objentry, 4)[0] if len(objentry) >= 8 else 0
    for i in range(count):
        at = 8 + 96 * i
        if at + 96 > len(objentry):
            break
        name = objentry[at + 8 : at + 0x28].split(b"\0")[0].decode("latin-1")
        out[struct.unpack_from("<I", objentry, at)[0]] = (name, objentry[at + 4])
    return out


def spawn_sets(
    ard: bytes,
    walks: dict[tuple[str, int], list[tuple[int, list[tuple[float, float, float]]]]] | None = None,
    arguments: dict[tuple[str, int, int], int] | None = None,
) -> list[tuple[str, list[tuple[int, list[tuple[int, float, float, float, float]]]]]]:
    """An area's spawn files (its .ard's BAR entries of type 0xc; OpenKH's ARD page, "Spawn Point": a u32 type, 2, and
    a u32 count of sets) as (the entry's name, its sets), each set (its type, its objects): a 0x2c-byte descriptor --
    + 0 its type, + 4 its objects' count, + 6 its event activators', + 8 its walk paths', + 0xa its return
    parameters', + 0xc its signals' -- then its objects (0x40 bytes each: + 0 the object entry's number, + 4 its place
    x, y, z, KH2's, + 0x10 its rotation x, y, z), its activators (0x40 each), its walk paths (8 bytes, + 2 their
    points' count, then the points, 12 bytes each), its return parameters (0x10 each) and its signals (8 each). Each
    object as (number, x, y, z, its turn about y); a set running past its entry ends that entry's list. `walks`, when
    given, gets each set's walk paths by (its entry's name, its set's place): (+ 0 its number, its points as x, y, z)
    each -- what KH2's 1:177 puts an object of the set on (FUN_1403ff460 finds one by its number, any for -1).
    `arguments`, when given, gets each object's first argument by (its entry's name, its set's place, its place in the
    set): its record's u32 at + 0x20 (+ 0x1c a u16, + 0x1e its serial), YS::OBJ's UnitArg[1], which 1:39 gives its
    script (Oogie Boogie fights only at 1, the hyenas are told apart by theirs)."""
    out = []
    if ard[:4] != kh2_bdx.BAR:
        return out
    for k in range(struct.unpack_from("<I", ard, 4)[0]):
        if 16 + 16 * (k + 1) > len(ard):
            break
        kind, _, name, at, size = struct.unpack_from("<HH4sII", ard, 16 + 16 * k)
        if kind != SPAWN_ENTRY or size < 8 or at + size > len(ard):
            continue
        spawn = ard[at : at + size]
        sets = []
        entry = name.rstrip(b"\0").decode("latin-1")
        p = 8
        for place in range(struct.unpack_from("<I", spawn, 4)[0]):
            if p + 0x2C > len(spawn):
                break
            set_type = spawn[p]
            objects, activators, paths, returns = struct.unpack_from("<4h", spawn, p + 4)
            signals = struct.unpack_from("<I", spawn, p + 0xC)[0]
            p += 0x2C
            if p + 0x40 * (objects + activators) > len(spawn):
                break
            placed = []
            for k in range(objects):
                number, x, y, z, _, yaw, _ = struct.unpack_from("<I6f", spawn, p + 0x40 * k)
                placed.append((number, x, y, z, yaw))
                if arguments is not None:
                    arguments[(entry, place, k)] = struct.unpack_from("<i", spawn, p + 0x40 * k + 0x20)[0]
            p += 0x40 * (objects + activators)
            ways = []
            for _ in range(paths):
                if p + 8 > len(spawn):
                    break
                number, count = struct.unpack_from("<hH", spawn, p)
                if p + 8 + 12 * count <= len(spawn):
                    ways.append((number, [struct.unpack_from("<3f", spawn, p + 8 + 12 * i) for i in range(count)]))
                p += 8 + 12 * count
            if walks is not None and ways:
                walks[(entry, place)] = ways
            p += 0x10 * returns + 8 * signals
            sets.append((set_type, placed))
        out.append((entry, sets))
    return out


def companions(
    ards: list[bytes], entries: dict[int, tuple[str, int]], name: str, kinds: set[str]
) -> list[tuple[str, float, float, float, float]]:
    """The objects KH2 places with a boss (its entry's kind 3 or 0xc) as its battle begins that are no enemies -- the
    Hydra's Phil, Pegasus and Phil's urn: its area's battle spawn ("b_..") whose set holds it and the player (its
    fight's own), the other objects of that spawn's sets KH2 places as the area loads (types 1 and 2; type 5 holds
    those its scripts make, 1:0) but the player and his party, enemies and bosses (a mission's author places those:
    the Volcanic Lord's Blizzard Lord) and those the mod has no kind of; each as (name, x, y, z, yaw) in its own frame:
    from it, turned so that the way from it to the player is + z (the port turns them to where its player stands).
    The first such spawn of the areas as given; none when none holds it, or for an enemy."""
    found = battle_of(ards, entries, name)
    if found is None:
        return []
    placed, _, me, turn = found
    c, s = math.cos(turn), math.sin(turn)
    out = []
    for _, objects in placed:
        for o in objects:
            other, kind = entries.get(o[0], ("", -1))
            if o is me or kind in PARTY or kind in ENEMIES or other not in kinds:
                continue
            dx, dy, dz = o[1] - me[1], o[2] - me[2], o[3] - me[3]
            out.append((other, dx * c - dz * s, dy, dx * s + dz * c, o[4] - turn))
    return out


def battle_of(ards: list[bytes], entries: dict[int, tuple[str, int]], name: str, walking: bool = False):
    """A boss's battle in its area's spawns (companions, walk_paths): the first battle spawn ("b_..") whose set of
    those KH2 places as the area loads holds it and the player -- that spawn's placed sets, its own set's walk paths,
    its own record (number, x, y, z, yaw) and the turn that makes the way from it to the player + z; None when none
    does, or for an enemy (its entry's kind neither 3 nor 0xc). `walking`: the first whose own set has walk paths, the
    player in any of the spawn's placed sets (the hyenas' second fight, lk05's b_60, keeps him in another)."""
    if next((kind for n, kind in entries.values() if n == name), -1) not in BOSSES:
        return None
    for ard in ards:
        walks: dict[tuple[str, int], list[tuple[int, list[tuple[float, float, float]]]]] = {}
        for entry, sets in spawn_sets(ard, walks):
            if not entry.startswith("b"):
                continue
            placed = [s for s in sets if s[0] in PLACED_SETS]
            players = [o for _, objects in placed for o in objects if entries.get(o[0], ("", -1))[1] == 0]
            own = next(
                (
                    (place, objects)
                    for place, (kind, objects) in enumerate(sets)
                    if kind in PLACED_SETS
                    and any(entries.get(o[0], ("", -1))[0] == name for o in objects)
                    and (
                        walks.get((entry, place)) and players
                        if walking
                        else any(entries.get(o[0], ("", -1))[1] == 0 for o in objects)
                    )
                ),
                None,
            )
            if own is None:
                continue
            place, objects = own
            me = next(o for o in objects if entries.get(o[0], ("", -1))[0] == name)
            player = next((o for o in objects if entries.get(o[0], ("", -1))[1] == 0), players[0] if players else me)
            turn = math.atan2(player[1] - me[1], player[3] - me[3])
            return placed, walks.get((entry, place), []), me, turn
    return None


def unit_argument(ards: list[bytes], entries: dict[int, tuple[str, int]], name: str) -> int:
    """The first argument KH2's battle spawn gives a boss (spawn_sets' `arguments`, YS::OBJ's UnitArg[1], 1:39): its
    record in the first battle spawn ("b_..") of the areas that places it; 0 for none (an enemy, or a boss no battle
    spawn places), as Days's waves give."""
    if next((kind for n, kind in entries.values() if n == name), -1) not in BOSSES:
        return 0
    for ard in ards:
        arguments: dict[tuple[str, int, int], int] = {}
        for entry, sets in spawn_sets(ard, arguments=arguments):
            if not entry.startswith("b"):
                continue
            for place, (kind, objects) in enumerate(sets):
                for k, o in enumerate(objects):
                    if kind in PLACED_SETS and entries.get(o[0], ("", -1))[0] == name:
                        return arguments.get((entry, place, k), 0)
    return 0


def walk_paths(
    ards: list[bytes], entries: dict[int, tuple[str, int]], name: str
) -> list[tuple[int, list[tuple[float, float, float]]]]:
    """The walk paths of a boss's battle set (battle_of), each (its number, its points) in the boss's own frame as
    companions gives its objects: from it, turned so that the way from it to the player is + z. Shenzi, Banzai and Ed
    run along theirs (1:177, 5:14)."""
    found = battle_of(ards, entries, name, walking=True)
    if found is None:
        return []
    _, ways, me, turn = found
    c, s = math.cos(turn), math.sin(turn)
    return [
        (number, [((x - me[1]) * c - (z - me[3]) * s, y - me[2], (x - me[1]) * s + (z - me[3]) * c) for x, y, z in points])
        for number, points in ways
    ]


def area_programs(ard: bytes) -> list[list[tuple[int, bytes]]]:
    """An area's scripts' programs (its .ard's BAR entries of type 0xd -- map, btl, evt; OpenKH's ARD page, "Script":
    programs {s16 id, s16 length, then operations {s16 code, s16 count, count u32 parameters}}), each its operations
    as (code, parameters)."""
    out = []
    if ard[:4] != kh2_bdx.BAR:
        return out
    for k in range(struct.unpack_from("<I", ard, 4)[0]):
        if 16 + 16 * (k + 1) > len(ard):
            break
        kind, _, _, at, size = struct.unpack_from("<HH4sII", ard, 16 + 16 * k)
        if kind != SCRIPT_ENTRY or at + size > len(ard):
            continue
        p = at
        while p + 4 <= at + size:
            length = struct.unpack_from("<h", ard, p + 2)[0]
            if length <= 4 or p + length > at + size:
                break
            ops, q = [], p + 4
            while q + 4 <= p + length:
                code, count = struct.unpack_from("<hh", ard, q)
                ops.append((code, ard[q + 4 : q + 4 + 4 * count]))
                q += 4 + 4 * count
            out.append(ops)
            p += length
    return out


def mission_of(ards: list[bytes], entries: dict[int, tuple[str, int]], name: str) -> str | None:
    """The mission KH2 runs with a boss's battle: in the first area whose battle spawn holds it, the program that
    places that spawn (operation 0, its name) and names a mission (operation 0x15: a u32, the mission's id, then its
    msn file's name); none for an enemy, or when no program does (Demyx's he17: HE17_MS105)."""
    if next((kind for n, kind in entries.values() if n == name), -1) not in BOSSES:
        return None
    for ard in ards:
        for entry, sets in spawn_sets(ard):
            if not entry.startswith("b") or not any(
                entries.get(o[0], ("", -1))[0] == name
                for kind, objects in sets
                if kind in PLACED_SETS
                for o in objects
            ):
                continue
            for ops in area_programs(ard):
                spawns = {params[:4].split(b"\0")[0] for code, params in ops if code == SPAWN_OP}
                missions = [params[4:].split(b"\0")[0] for code, params in ops if code == MISSION_OP]
                if entry.encode("latin-1") in spawns and missions and missions[0]:
                    return missions[0].decode("latin-1")
            return None
    return None


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


# Kinds the journal names only by another of the same character's objects: FINAL MIX's hooded Roxas (B_EX390) has no
# entry of his own, the Roxas of the prologue (P_EX110) has
JOURNAL_ALIASES = {"B_EX390": "P_EX110"}


def journal_title(titles: dict[str, int], name: str) -> int | None:
    """A kind's title in the journal: its own model's, else its journal's (_GM: the Volcanic Lord's B_AL100_FIRE is
    the journal's B_AL100_FIRE_GM), else its family's -- the model's first two parts (Hades of the cups' B_HE030_CLSM
    is the journal's B_HE030_GM) --, else the journal's first model named on from it (the Wight Knight's M_EX200 is
    the journal's M_EX200_NM), else its character's other object's (JOURNAL_ALIASES); none when the journal has none."""
    family = "_".join(name.upper().split("_")[:2])
    alias = JOURNAL_ALIASES.get(name.upper(), "")
    for candidate in (name.upper(), f"{name.upper()}_GM", f"{family}_GM", family, alias):
        if candidate in titles:
            return titles[candidate]
    return next((title for model, title in titles.items() if model.startswith(f"{name.upper()}_")), None)


def copy_pages(source: Path, to: Path, patterns: str | tuple[str, ...] = ("-*.dds", "-*.png")) -> int:
    """An entry's HD pages (-N.dds, or -N.png where the collection has PNGs: Zexion's B_EX370.a.us, his effects' and
    his command menu's; a mission's <LANGUAGE>_<name>_barN.png) into `to`: how many."""
    if not source.is_dir():
        return 0
    to.mkdir(parents=True, exist_ok=True)
    pages = sorted({page for pattern in ((patterns,) if isinstance(patterns, str) else patterns)
                    for page in source.glob(pattern)})
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


def script_sounds(bar: bytes) -> set[int]:
    """The sounds a BAR's scripts play themselves (1:318 trap_obj_play_se, 6:70 trap_obj_play_se_loop: the object and
    KH2's sound id, bank x 1000 + number), pushed right before or given to a routine that passes them on: Zexion's
    book's pages."""
    out = set()
    for _, s in kh2_bdx.scripts_in(bar):
        code = kh2_bdx.walk(s)
        for call in ((1, 318), (6, 70)):
            out |= {v for v in kh2_bdx.argument_values(s, code, call, 2, 1) if v > 0}
    return out


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
    kh2: Path,
    kh2_exe: Path,
    mod: Path,
    exe: Path | None = None,
    run: Callable[[list[str]], str] = run_exe,
    only: set[str] | None = None,
) -> list[str]:
    """The calls' shapes, each kind's files and the list into mod/; what was left out, each with why. `only`: those
    objects alone (any of KH2's obj/, named or not in KINDS), exactly as asked -- a mod carrying the enemies its own
    missions use; the objects one of them makes (1:0) or KH2 places with it that were not asked for are told, not
    added. The files every kind needs (the calls' shapes, SHARED, the names, the command menu, the font, the
    resident effect pictures, the keyblade's) go in whatever was asked."""
    left_out = []
    shapes = kh2_bdx.exe_calls(kh2_exe.read_bytes())
    # the walks read a routine's stack past these calls (kh2_bdx.SHAPES): Zexion's book and illusion, made by a routine
    # given their numbers after a 1:84, were not found while none was known
    kh2_bdx.SHAPES.update({(kind, number): (count, int(back)) for kind, number, count, back in shapes})
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
        "# name\ttitle\tcovered\tcalls\tmissing\tplays the player\tjournal name\tplaced with it\tmission\twalk paths"
        "\thome map\troom objects\tunit argument (kh2_enemies.py; the port's kh2_enemies.txt)"
    ]
    entries_file = kh2 / "original" / "00objentry.bin"
    entries = entries_file.read_bytes() if entries_file.is_file() else b""
    numbered = entry_kinds(entries)
    named_areas = [(a.stem, a.read_bytes()) for a in sorted((kh2 / "original" / AREAS).glob("*.ard"))]
    areas = [ard for _, ard in named_areas]
    if not areas:
        left_out.append(f"original/{AREAS}/*.ard (KH2's areas): not there; no boss brings what KH2 places with it")
    jiminy_file = kh2 / "original" / "menu" / "us" / JOURNAL
    titles = journal_titles(jiminy_file.read_bytes(), entries) if jiminy_file.is_file() else {}
    if not jiminy_file.is_file():
        left_out.append(f"menu/us/{JOURNAL} (the enemies' names): not there; their stand-ins' names show")
    homes: dict[str, str] = {}
    rooms: dict[str, list[tuple[str, float, float, float, float, int]]] = {}
    making = dict(KINDS)
    for name, title in KINDS.items():
        script = kh2 / "original" / "obj" / f"{name}.mdlx"
        if not script.is_file():
            continue
        area, pieces = room_objects(named_areas, numbered, name, searched_numbers(script.read_bytes()))
        if area is None:
            continue
        if not ROOM_BOSSES:
            del making[name]
            left_out.append(f"{name} ({title}): fights with its KH2 room's pieces ({area}): left out for now")
            continue
        homes[name], rooms[name] = area, pieces
        # its room's objects, and the field objects its script makes (1:0: Oogie Boogie's gift boxes), bound as it is
        made = [numbered[n][0] for n in sorted(made_numbers(script.read_bytes())) if n in numbered]
        for piece in [p for p, *_ in pieces] + [m for m in made if m.startswith("F_") and m not in KINDS]:
            if piece not in making:
                making[piece] = f"{piece} ({title}'s room)"
                homes[piece] = area
    # the objects each kind's script makes (1:0) go with it: the Lingering Will's B_EX430, Zexion's books and illusions
    # (until 2026-11-14 only the room bosses' were taken, so the Will's second object was never in the mod)
    todo = list(making)
    while todo:
        name = todo.pop()
        script = kh2 / "original" / "obj" / f"{name}.mdlx"
        if not script.is_file():
            continue
        for number in sorted(made_numbers(script.read_bytes())):
            made = numbered.get(number, ("", -1))[0]
            if made and made not in making:
                making[made] = f"{made} ({making[name]}'s)"
                if name in homes:  # bound to its maker's room
                    homes.setdefault(made, homes[name])
                todo.append(made)
    if only is not None:
        asked = {n.upper() for n in only}
        making = {n: making.get(n, KINDS.get(n, n)) for n in sorted(asked)}
        for name in making:
            script = kh2 / "original" / "obj" / f"{name}.mdlx"
            if not script.is_file():
                continue
            needs = {numbered[n][0] for n in made_numbers(script.read_bytes()) if n in numbered}
            needs |= {c[0] for c in companions(areas, numbered, name, set(KINDS))}
            for other in sorted(needs - set(making)):
                left_out.append(f"{name} makes or brings {other}, not asked for (--only): its script finds none")
    for name, title in making.items():
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
        sounds = (
            (motion_sounds(sources[1].read_bytes()) if sources[1].is_file() else set())
            | (effect_sounds(sources[2].read_bytes()) if sources[2].is_file() else set())
            | script_sounds(sources[0].read_bytes())
        )
        left_out += make_sounds(kh2, name, sounds, mod / "kh2" / "se")
        left_out += make_voices(kh2, name, VOICES.get(name), mod / "kh2" / "voice" / name)
        calls = calls_in(sources[0].read_bytes())
        with_it = companions(areas, numbered, name, set(KINDS))
        mission = mission_of(areas, numbered, name)
        if mission is not None:
            source = kh2 / "original" / MISSIONS / f"{mission}.bar"
            if source.is_file():
                (mod / "kh2" / "msn").mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, mod / "kh2" / "msn" / source.name)
                # its pieces' HD pictures, one for each of its IMGDs in their order (US_<name>_bar0.png ...): KH2 PC
                # draws the pieces' sequences with them, its PS2 pictures laid out otherwise
                copy_pages(kh2 / "remastered" / MISSIONS / source.name, mod / "kh2" / "msn" / mission, "*_bar*.png")
            else:
                left_out.append(f"{name} ({title}): its mission {source.name} not there")
                mission = None
        ways = walk_paths(areas, numbered, name)
        listed.append(
            kinds_line(
                name, title, calls, handled, journal_title(titles, name), with_it, mission, ways, homes.get(name),
                rooms.get(name), unit_argument(areas, numbered, name),
            )
        )
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
    ap.add_argument(
        "--only",
        nargs="+",
        metavar="NAME",
        help="these objects alone (KH2's obj/<NAME>.mdlx: B_EX390 W_EX010_ROXAS_LIGHT ...), into a mod of your own "
        "that carries the enemies its missions use; what they need and were not given is told",
    )
    args = ap.parse_args(argv)
    try:
        left_out = make(
            args.kh2, args.kh2_exe, Path("mods") / args.mod, args.exe, only=set(args.only) if args.only else None
        )
    except (OSError, RuntimeError, kh2_bdx.AiError) as e:
        print(f"kh2 enemies: {e}", file=sys.stderr)
        return 1
    for line in left_out:
        print(f"kh2 enemies: left out {line}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
