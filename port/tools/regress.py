#!/usr/bin/env python3
"""Play the native build through scripted input, without touching anyone's save: the regression
walks (port/tools/regress_walks/: --walk-save OLD.sav [--only NAME] [--record]) or one script for
traces (--input FILE --save PATH --log PATH). Each run is in a temporary directory with a link to
data/ and a copy of the save. docs/BUILD.md, "The regression walks".
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import save_file  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
WALKS_DIR = ROOT / "port" / "tools" / "regress_walks"
DEFAULT_EXE = ROOT / "build" / "port" / "obj-release-x64" / "khdays-native.exe"  # port/build.py --config Release
EXPECTED = WALKS_DIR / "expected.txt"


@dataclass
class Walk:
    name: str
    timeout: int  # seconds; a walk ends when its time is up
    player_save: bool  # the player's save (else the walks' own old save)
    saves: bool = False  # the walk saves the game (else the save must stay as it was)
    players: int = 1  # copies of the game playing it together, each with its script
    script: str | None = None  # the scripts' name, if not the walk's
    # "direct" or "relay": the players meet through a khdays-server of this
    # PC (the one next to EXE), not the local network; "relay" never tries
    # the host straight
    server: str | None = None
    mods: str | None = None  # one mod's folder (from the repository's root) it plays with, alone
    window: bool = False  # in a window (a mission's opening dialogs wait for the window's picture)
    marks: tuple[str, ...] = ()  # what each player's session log must say (KHDAYS_LOG on)


# The routes: the Mission Mode loop, Story Mode, a mission in play, challenge 07, a save (the camp
# menu's, a new file in slot 2), the camp's reset back to the camp, and multiplayer: two copies on
# this PC from the title to a mission's review, on the local network, then met through and relayed
# by a khdays-server of this PC.
WALKS = [
    Walk("mission", 250, player_save=False),
    Walk("story", 250, player_save=False),
    Walk("play", 200, player_save=False),
    Walk("run07", 300, player_save=True),
    Walk("save", 120, player_save=True, saves=True),
    Walk("reset", 180, player_save=False),
    Walk("multiplayer", 240, player_save=False, players=2),
    Walk("online", 240, player_save=False, players=2, script="multiplayer", server="direct"),
    Walk("relayed", 240, player_save=False, players=2, script="multiplayer", server="relay"),
    # four players: a host and three guests through a server of this PC
    Walk("multiplayer4", 300, player_save=False, players=4, script="multiplayer4", server="direct"),
    # the Stop magic (port/tests/mods_v2/stop): its three levels cast on held Shadows, their damage kept; Stop on a
    # Doublecast, Stopga on a Magic LV2
    Walk(
        "stop",
        150,
        player_save=False,
        mods="port/tests/mods_v2/stop",
        window=True,
        marks=(
            "casts 2 1 1, Magic LV 1 1 2",
            "stop: Stopga cast",
            "Magic LV 2 (300 ticks)",
            "stop: Stopra cast",
            "stop: Stop cast",
            "took its kept",
        ),
    ),
    # ... in a room of two (port/tests/mods_v2/stop_room): the host's casts and a guest's, on every copy; the room's
    # friendly fire on, each holds the other player
    Walk(
        "stop_room",
        300,
        player_save=False,
        players=2,
        mods="port/tests/mods_v2/stop_room",
        window=True,
        marks=(
            "Magic LV 1 1 1 (the room's)",
            "stop: player 1 held for 120 ticks",
            "stop: player 0 held for 120 ticks",
            "stop: player 1 held for 240 ticks",
            "stop: player 0 held for 180 ticks",
        ),
    ),
    # ... at the moogle (port/tests/mods_v2/stop_shop): its recipe listed and Stop synthesised at Mission Mode's camp
    Walk(
        "stop_shop",
        110,
        player_save=False,
        mods="port/tests/mods_v2/stop_shop",
        window=True,
        marks=("stop: recipes 101-103 added after the game's 101", "Stop synthesised\t1"),
    ),
]

# The runtime reports the current scene once a second, and at each change,
# on stderr:
#   khdays-native: frame 1200, scene 2 (object 020b1234, pending 0 arg 0)
SCENE_LINE = re.compile(r"^khdays-native: frame \d+, scene (\d+)")

# Lines that mean the run went wrong, whatever the scenes say.
# An AddressSanitizer report (the --asan build) is trouble too, and
# the game's memory the port could not reach (sound data it could
# not read, a file handle's header outside the headers' heaps, a heap full) --
# a silent title once passed every walk's scenes.
TROUBLE = re.compile(
    r"Run-Time Check|reached a call|exception 0x|outside the modelled|"
    r"no native function|gap|AddressSanitizer|played as silence|"
    r"outside the headers' heaps|could not give"
)
# The server's log: only a sanitizer's report means it went wrong.
SERVER_TROUBLE = re.compile(r"AddressSanitizer")


def player_saves(work: Path, player: int) -> Path:
    """Where player `player`'s copy of the game keeps its files in `work`:
    saves/, or saves/player<N>/ for a further player (host/app/instance.h)."""
    return work / "saves" if player == 1 else work / "saves" / f"player{player}"


def make_workdir(
    save: Path | None,
    mods: Path | None = None,
    settings: Path | None = None,
    player: int = 1,
    data: Path = ROOT / "data",
) -> Path:
    """A fresh directory with data/ (`data`) linked in and, if given, a copy of the save
    (where player `player` keeps it), the mods folder linked in as mods/ and a
    copy of a settings file as saves/settings.ini (screens = one for the
    one-screen walks)."""
    work = Path(tempfile.mkdtemp(prefix="khdays-run-"))
    link = work / "data"
    # A junction, not a copy: data/ is gigabytes. `mklink /J` needs no
    # administrator rights.
    subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(data.resolve())], check=True, capture_output=True)
    if mods is not None and (mods / "mod.ini").exists():
        # one mod's own folder: alone in a mods/ of the run's
        (work / "mods").mkdir()
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(work / "mods" / mods.resolve().name), str(mods.resolve())],
            check=True,
            capture_output=True,
        )
    elif mods is not None:
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(work / "mods"), str(mods.resolve())], check=True, capture_output=True
        )
    if save is not None:
        player_saves(work, player).mkdir(parents=True)
        shutil.copy2(save, player_saves(work, player) / "khdays.sav")
    if settings is not None:
        (work / "saves").mkdir(exist_ok=True)
        shutil.copy2(settings, work / "saves" / "settings.ini")
    return work


def remove_workdir(work: Path) -> None:
    """Delete the directory, removing the data/ and mods/ links first so that
    nothing they point to can ever be deleted with it."""
    mods = work / "mods"
    for link in (work / "data", mods):
        if link.exists() or os.path.lexists(link):
            if link == mods and not _is_link(mods):
                for one in mods.iterdir():
                    os.rmdir(one)  # one mod's link alone in mods/ (make_workdir)
            os.rmdir(link)  # removes the junction itself, never its target
    shutil.rmtree(work)


def _is_link(path: Path) -> bool:
    """Whether `path` is a junction or a symbolic link (not a folder of its own)."""
    try:
        return bool(os.lstat(path).st_file_attributes & 0x400)  # FILE_ATTRIBUTE_REPARSE_POINT
    except (OSError, AttributeError):
        return os.path.islink(path)


def keep_session_log(work: Path, to: Path) -> None:
    """The session log the game wrote in `work` (KHDAYS_LOG, saves/logs/ or a
    further player's saves/player<N>/logs/), copied to `to` before the
    directory goes."""
    if (work / "saves").exists():
        for session in sorted((work / "saves").rglob("session-*.log")):
            shutil.copy2(session, to)


def session_text(work: Path) -> str:
    """Every session log the game wrote in `work` (KHDAYS_LOG), as one text."""
    if not (work / "saves").exists():
        return ""
    return "".join(
        session.read_text(encoding="utf-8", errors="replace")
        for session in sorted((work / "saves").rglob("session-*.log"))
    )


def start_server(exe: Path, log: Path) -> tuple[subprocess.Popen, int]:
    """The khdays-server next to `exe` started on a port free now, its output to `log`: the process and its port."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
    probe.close()
    with log.open("wb") as out:
        # never the PC's router: a walk opens no port on it
        server = subprocess.Popen(
            [str(exe.parent / "khdays-server.exe"), "--port", str(port), "--router", "off"],
            stdin=subprocess.DEVNULL,
            stdout=out,
            stderr=subprocess.STDOUT,
        )
    return server, port


def run_game(
    exe: Path, work: Path, script: str, extra_env: dict[str, str], timeout: int, log: Path, window: bool
) -> int | None:
    """Run the game in `work` with `script` as its input until it exits or its
    time is up. stdout and stderr go to `log`. Returns the exit code, or None
    when the time ran out (the normal end of a walk)."""
    return run_games(exe, [(work, script, log)], extra_env, timeout, window)[0]


def run_games(
    exe: Path, players: list[tuple[Path, str, Path]], extra_env: dict[str, str], timeout: int, window: bool
) -> list[int | None]:
    """Run a copy of the game for each (work, script, log), all at once --
    player 1, 2, ... in that order, as the copies of a multiplayer walk -- each
    until it exits or the time is up. Returns each one's exit code, or None
    when the time ran out (the normal end of a walk)."""
    games = []
    try:
        for player, (work, script, log) in enumerate(players, start=1):
            env = dict(os.environ)
            env["KHDAYS_INPUT"] = script
            # the player this copy is, whatever copies of the port run on the
            # PC (host/app/instance.h): a walk alone is player 1
            env.setdefault("KHDAYS_PLAYER_SLOT", "1")
            if len(players) > 1:
                env["KHDAYS_PLAYER_SLOT"] = str(player)
            # no walk reaches the internet: the public server only when a
            # walk names a server of this PC
            env.setdefault("KHDAYS_NET_SERVER", "")
            if not window:
                env["KHDAYS_HEADLESS"] = "1"
            # --env N:NAME=VALUE is player N's alone (a guest under a testing
            # knob the host has not, e.g. KHDAYS_DS_MEMORY)
            for name, value in extra_env.items():
                slot, sep, own = name.partition(":")
                if not sep:
                    env[name] = value
                elif slot == str(player):
                    env[own] = value
            with log.open("wb") as out:
                games.append(subprocess.Popen([str(exe)], cwd=work, env=env, stdout=out, stderr=subprocess.STDOUT))
        deadline = time.monotonic() + timeout
        codes: list[int | None] = []
        for game in games:
            try:
                codes.append(game.wait(timeout=max(0.0, deadline - time.monotonic())))
            except subprocess.TimeoutExpired:
                codes.append(None)
        return codes
    finally:
        for game in games:
            if game.poll() is None:
                game.kill()
                game.wait()


def scenes_and_trouble(log: Path) -> tuple[list[str], str | None]:
    """The scenes the log went through (repeats removed) and its first line of trouble, if any.
    Scene 0 between two scenes (one ended, the next not begun) is dropped: whether the report falls
    on it depends on timing. A walk that ends in scene 0 keeps it."""
    scenes: list[str] = []
    trouble = None
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SCENE_LINE.match(line)
        if match and (not scenes or scenes[-1] != match.group(1)):
            if scenes and scenes[-1] == "0":
                scenes.pop()  # it was only between two scenes
            if not scenes or scenes[-1] != match.group(1):
                scenes.append(match.group(1))
        if trouble is None and TROUBLE.search(line):
            trouble = line.strip()
    return scenes, trouble


def first_line(log: Path, pattern: re.Pattern[str]) -> str | None:
    """The first line of `log` that `pattern` finds, if any."""
    if not log.exists():
        return None
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if pattern.search(line):
            return line.strip()
    return None


def read_expected() -> dict[str, list[str]]:
    expected: dict[str, list[str]] = {}
    if EXPECTED.exists():
        for line in EXPECTED.read_text(encoding="utf-8").splitlines():
            if ":" in line and not line.startswith("#"):
                name, scenes = line.split(":", 1)
                expected[name.strip()] = scenes.split()
    return expected


def run_walks(args: argparse.Namespace) -> int:
    expected = read_expected()
    walks = [w for w in WALKS if args.only is None or w.name == args.only]
    if not walks:
        sys.exit(f"no walk named {args.only}")
    results: dict[str, list[str]] = {}
    failed = False
    for walk in walks:
        save = args.player_save if walk.player_save else args.walk_save
        # one copy of the game per player, each in its own directory with its
        # script (<walk>.input, or <walk>_<player>.input for several) and its
        # copy of the save; every player must go through the same scenes
        names = [walk.name] if walk.players == 1 else [f"{walk.name}_{n}" for n in range(1, walk.players + 1)]
        base = walk.script or walk.name
        scripts = [base] if walk.players == 1 else [f"{base}_{n}" for n in range(1, walk.players + 1)]
        works: list[Path] = []
        server = None
        try:
            players = []
            mods = ROOT / walk.mods if walk.mods is not None else args.mods
            for player, (name, script_name) in enumerate(zip(names, scripts), start=1):
                works.append(make_workdir(save, mods, args.settings, player, args.data))
                script = (WALKS_DIR / f"{script_name}.input").read_text(encoding="utf-8").strip()
                players.append((works[-1], script, works[-1] / "game.log"))
            env = extra_env_of(args)
            if walk.marks:
                env["KHDAYS_LOG"] = "1"
            if walk.server is not None:
                server, port = start_server(args.exe, works[0] / "server.log")
                env.update({"KHDAYS_NET_SERVER": f"127.0.0.1:{port}", "KHDAYS_NET_LAN": "0"})
                if walk.server == "relay":
                    env["KHDAYS_NET_PUNCH"] = "0"
            exit_codes = run_games(args.exe, players, env, walk.timeout, window=walk.window)
            outcomes = []
            if server is not None:
                server.kill()
                server.wait()
                server_trouble = first_line(works[0] / "server.log", SERVER_TROUBLE)
                if server_trouble is not None:
                    outcomes.append((f"{walk.name} server", [], server_trouble, None, None))
            for player, (name, work, exit_code) in enumerate(zip(names, works, exit_codes), start=1):
                log = work / "game.log"
                kept_save = player_saves(work, player) / "khdays.sav"
                scenes, trouble = scenes_and_trouble(log)
                missing = [mark for mark in walk.marks if mark not in session_text(work)]
                verdict = save_check(walk, save, kept_save) or (f"MISSING {missing[0]!r}" if missing else None)
                outcomes.append((name, scenes, trouble, exit_code, verdict))
                if args.logs is not None:
                    args.logs.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(log, args.logs / f"{name}.log")
                    keep_session_log(work, args.logs / f"{name}.session.log")
                    if kept_save.exists():
                        shutil.copy2(kept_save, args.logs / f"{name}.sav")
        finally:
            if server is not None:
                if server.poll() is None:
                    server.kill()
                    server.wait()
                if args.logs is not None and (works[0] / "server.log").exists():
                    shutil.copy2(works[0] / "server.log", args.logs / f"{walk.name}.server.log")
            for work in works:
                remove_workdir(work)
        results[walk.name] = outcomes[0][1]
        want = expected.get(walk.name)
        for name, scenes, trouble, exit_code, save_verdict in outcomes:
            verdict = "ok"
            if trouble is not None:
                verdict = "TROUBLE"
            elif exit_code is not None:
                verdict = f"EXITED ({exit_code})"
            elif want is None:
                verdict = "no expected sequence yet"
            elif scenes != want:
                verdict = "DIFFERENT (expected: " + " ".join(want) + ")"
            elif save_verdict is not None:
                verdict = save_verdict
            if verdict != "ok" and not (args.record and verdict == "no expected sequence yet"):
                failed = True
            print(f"{name:13} {verdict:10} scenes: {' '.join(scenes)}")
            if trouble is not None:
                print(f"              first trouble line: {trouble}")
    if args.record:
        merged = read_expected()
        merged.update(results)
        lines = ["# The scene sequence each regression walk must produce (port/tools/regress.py)."]
        lines += [f"{w.name}: {' '.join(merged[w.name])}" for w in WALKS if w.name in merged]
        EXPECTED.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        print(f"recorded {EXPECTED.relative_to(ROOT)}")
    return 1 if failed else 0


def save_check(walk: Walk, before: Path | None, after: Path) -> str | None:
    """None if the save is as the walk must leave it: untouched, or -- for the
    walk that saves -- changed, every copy the game signed whole."""
    if before is None:
        return None
    old = before.read_bytes()
    new = after.read_bytes() if after.exists() else b""
    if not walk.saves:
        return None if new == old else "SAVE WRITTEN (nothing should save)"
    if new == old:
        return "NOT SAVED"
    broken = save_file.broken_copies(new)
    return None if not broken else f"SAVE BROKEN (copy at 0x{broken[0]:05x})"


def extra_env_of(args: argparse.Namespace) -> dict[str, str]:
    """The --env NAME=VALUE pairs."""
    extra_env = {}
    for item in args.env:
        name, sep, value = item.partition("=")
        if not sep:
            sys.exit(f"--env takes NAME=VALUE, not {item}")
        extra_env[name] = value
    return extra_env


def run_script(args: argparse.Namespace) -> int:
    script = " ".join(args.input.read_text(encoding="utf-8").split())
    extra_env = extra_env_of(args)
    work = make_workdir(args.save, args.mods, args.settings, data=args.data)
    try:
        exit_code = run_game(args.exe, work, script, extra_env, args.timeout, args.log, args.window)
        keep_session_log(work, args.log.with_suffix(".session.log"))
    finally:
        if args.keep:
            print(f"kept the working directory: {work}")
        else:
            remove_workdir(work)
    scenes, trouble = scenes_and_trouble(args.log)
    ended = "time ran out" if exit_code is None else f"exited with {exit_code}"
    print(f"{ended}; scenes: {' '.join(scenes)}; log: {args.log}")
    if trouble is not None:
        print(f"first trouble line: {trouble}")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "exe",
        type=Path,
        nargs="?",
        default=DEFAULT_EXE,
        help=f"the khdays-native.exe to run (default: {DEFAULT_EXE.relative_to(ROOT)})",
    )
    parser.add_argument(
        "--data", type=Path, default=ROOT / "data", help="the data folder to play from (another extraction's)"
    )
    walks = parser.add_argument_group("regression walks")
    walks.add_argument("--walk-save", type=Path, help="the save the mission, story and play walks start from")
    walks.add_argument(
        "--player-save",
        type=Path,
        default=ROOT / "saves" / "khdays.sav",
        help="the save the run07 walk starts from (default: saves/khdays.sav)",
    )
    walks.add_argument("--only", help="run only this walk")
    walks.add_argument(
        "--record", action="store_true", help="write the scene sequences of this run to regress_walks/expected.txt"
    )
    walks.add_argument("--logs", type=Path, help="keep each walk's log in this directory")
    parser.add_argument(
        "--mods",
        type=Path,
        help="a mods folder to play with, linked in as mods/, or one mod's own folder, alone in mods/ (the walks or the "
        "script)",
    )
    parser.add_argument(
        "--settings",
        type=Path,
        help="a settings file the game starts with, copied as saves/settings.ini (the walks or the script)",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        help="an extra NAME=VALUE for the game (the walks or the script); N:NAME=VALUE for player N alone",
    )
    one = parser.add_argument_group("one script")
    one.add_argument("--input", type=Path, help="a file holding a KHDAYS_INPUT script")
    one.add_argument("--save", type=Path, help="the save to start from (copied; none = a new game)")
    one.add_argument("--log", type=Path, help="where the game's output goes")

    one.add_argument("--timeout", type=int, default=300, help="seconds before the run is stopped")
    one.add_argument("--window", action="store_true", help="open the window instead of running headless")
    one.add_argument("--keep", action="store_true", help="keep the working directory (shots, saves)")
    args = parser.parse_args()

    if not args.exe.exists():
        sys.exit(f"{args.exe} does not exist")
    args.exe = args.exe.resolve()
    if args.input is not None:
        if args.log is None:
            sys.exit("--input needs --log")
        return run_script(args)
    if args.walk_save is None:
        sys.exit("the walks need --walk-save (the old save the first three walks start from)")
    return run_walks(args)


if __name__ == "__main__":
    sys.exit(main())
