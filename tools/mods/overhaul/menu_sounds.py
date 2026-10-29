"""The settings menu's cues (menu_sounds/se_sys_*.wav: host/settings/settings_sample.cpp plays them): the game's own
menu sounds, COMMON_SYS's sequences 0-3, rendered by the port from the game's sound archive (khdays-native
--dump-sounds DIR NAME...), whole and at 0 dB as the game's sequencer plays them; the menu puts the sequence's
volume on top. They go to menu_sounds/, not sounds/, where the mods' replacements would change the game's own."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
from pathlib import Path

from common import Inputs

CUES = ("se_sys_click", "se_sys_kettei", "se_sys_open", "se_sys_cancel")


def make(inputs: Inputs, mod: Path) -> None:
    if inputs.exe is None:
        raise SystemExit("the menu's sounds are rendered by the port: give --exe")
    out = mod / "menu_sounds"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        names = [f"seqarc/COMMON_SYS/{cue}" for cue in CUES]
        # (the port finds the game's data from its working folder: the install's, above data/extracted/<hash>)
        done = subprocess.run(
            [str(inputs.exe), "--dump-sounds", temp, *names], cwd=inputs.data.parents[2], capture_output=True, text=True
        )
        if done.returncode != 0:
            raise SystemExit(f"khdays-native --dump-sounds: {(done.stdout + done.stderr).strip()[-400:]}")
        for cue, name in zip(CUES, names):
            shutil.copyfile(Path(temp) / f"{name}.wav", out / f"{cue}.wav")
