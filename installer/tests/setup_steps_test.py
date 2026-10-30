"""The installer helper's foundations (setup_steps.py): whole-or-absent outputs, the install's state,
the result file and the exit codes."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import setup_steps as s  # noqa: E402


@s.command("test-ok", "a test command that succeeds", lambda p: p.add_argument("value"))
def _ok(args):
    return {"value": args.value}


@s.command("test-refuse", "a test command the player can put right", lambda p: None)
def _refuse(args):
    raise s.Refusal("failed", what="a refusal")


@s.command("test-fail", "a test command that breaks", lambda p: None)
def _fail(args):
    raise RuntimeError("broken")


class Making(unittest.TestCase):
    def test_whole_on_success(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "data" / "out"
            with s.making(final) as work:
                work.mkdir()
                (work / "f").write_text("x")
                self.assertFalse(final.exists())
            self.assertEqual((final / "f").read_text(), "x")
            self.assertFalse(s.partial_name(final).exists())

    def test_absent_on_error(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "out"
            with self.assertRaises(RuntimeError):
                with s.making(final) as work:
                    work.mkdir()
                    raise RuntimeError("interrupted")
            self.assertFalse(final.exists())

    def test_replaces_an_older_one_and_a_leftover(self):
        with tempfile.TemporaryDirectory() as d:
            final = Path(d) / "out"
            final.mkdir()
            (final / "old").write_text("1")
            s.partial_name(final).mkdir()  # left by an interrupted run
            with s.making(final) as work:
                self.assertFalse(work.exists())
                work.mkdir()
                (work / "new").write_text("2")
            self.assertEqual(sorted(p.name for p in final.iterdir()), ["new"])
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ["out"])


def _older_one(d: str) -> tuple[Path, Path]:
    """An older cutscenes folder and a newly made one: a video replaced, one gone, one added."""
    final, made = Path(d) / "cutscenes", Path(d) / "made"
    (final / "cinematics").mkdir(parents=True)
    (final / "cinematics" / "a.mp4").write_text("old")
    (final / "cinematics" / "gone.mp4").write_text("old")
    (made / "cinematics").mkdir(parents=True)
    (made / "cinematics" / "a.mp4").write_text("new")
    (made / "cinematics" / "b.mp4").write_text("new")
    return final, made


@unittest.skipUnless(sys.platform == "win32", "Windows keeps folders shown in its File Explorer from being renamed")
class PutInPlace(unittest.TestCase):
    def test_a_folder_shown_in_the_file_explorer(self):
        import ctypes
        from ctypes import wintypes

        k = ctypes.WinDLL("kernel32", use_last_error=True)
        k.CreateFileW.restype = wintypes.HANDLE
        k.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        k.CloseHandle.argtypes = [wintypes.HANDLE]
        with tempfile.TemporaryDirectory() as d:
            final, made = _older_one(d)
            # opened the way a File Explorer window watches the folder it shows
            watched = k.CreateFileW(str(final / "cinematics"), 1, 7, None, 3, 0x02000000, None)
            try:
                s.put_in_place(made, final)
            finally:
                k.CloseHandle(watched)
            self.assertEqual(sorted(p.name for p in (final / "cinematics").iterdir()), ["a.mp4", "b.mp4"])
            self.assertEqual((final / "cinematics" / "a.mp4").read_text(), "new")
            self.assertEqual(sorted(p.name for p in Path(d).iterdir()), ["cutscenes"])

    def test_a_video_the_game_plays(self):
        with tempfile.TemporaryDirectory() as d:
            final, made = _older_one(d)
            with open(final / "cinematics" / "a.mp4"), self.assertRaises(s.Refusal) as refused:
                s.put_in_place(made, final)
            self.assertEqual(refused.exception.key, "in-use")
            self.assertIn("a.mp4", refused.exception.values["file"])


class State(unittest.TestCase):
    def test_rules(self):
        with tempfile.TemporaryDirectory() as d:
            install = Path(d)
            self.assertEqual(s.load_state(install), {})
            state = {}
            inputs = {"rom": "abc"}
            self.assertFalse(s.step_done(state, "data", inputs))
            s.mark_step(state, "data", inputs, folder="data/extracted/abc")
            s.save_state(install, state)
            again = s.load_state(install)
            self.assertTrue(s.step_done(again, "data", inputs))
            self.assertFalse(s.step_done(again, "data", {"rom": "other"}))  # inputs changed: redo
            self.assertFalse(s.step_done(again, "hd", inputs))

    def test_hd_made_is_left_as_it_is(self):
        with tempfile.TemporaryDirectory() as d:
            install = Path(d)
            made = install / "mods" / s.HD_MOD
            state = {}
            inputs = {"1.5+2.5": r"z:\games\KH 1.5", "2.8": r"z:\games\KH 2.8"}
            s.mark_step(state, "hd-menus", inputs, made_by=s.made_by("hd-menus"))
            folders = {"1.5+2.5": "Z:\\Games\\kh 1.5\\", "2.8": r"Z:\Games\KH 2.8"}  # as Windows compares them
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))  # nothing there: made again
            made.mkdir(parents=True)
            (made / "layouts").mkdir()
            self.assertTrue(s.hd_made(state, "hd-menus", folders, made))
            # made by other tools, or by an installer that did not keep them (0.1.2's): made again
            s.mark_step(state, "hd-menus", inputs, made_by="another installer's tools")
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))
            s.mark_step(state, "hd-menus", inputs)
            self.assertFalse(s.hd_made(state, "hd-menus", folders, made))
            self.assertNotEqual(s.made_by("hd-menus"), s.made_by("hd-videos"))
            s.mark_step(state, "hd-menus", inputs, made_by=s.made_by("hd-menus"))
            self.assertFalse(s.hd_made(state, "hd-menus", {**folders, "2.8": r"Z:\Other"}, made))
            self.assertFalse(s.hd_made(state, "hd-videos", folders, made))
            # the command: everything asked for already made, so no collection is read
            s.save_state(install, state)
            result = s.build_hd(
                s.argparse.Namespace(
                    install=install,
                    menus=True,
                    videos=False,
                    c1525=Path(folders["1.5+2.5"]),
                    c28=Path(folders["2.8"]),
                    language="en",
                )
            )
            self.assertTrue(result["skipped"])


class HdTools(unittest.TestCase):
    def test_a_failing_tool_leaves_the_others_made(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            (folder / "good.py").write_text("print('made')\n", encoding="utf-8")
            (folder / "bad.py").write_text("raise ValueError('zero-size array')\n", encoding="utf-8")
            log = folder / "setup" / s.HD_ERRORS

            def line_of(tool, template):
                return [sys.executable, str(folder / tool), *template]

            pieces = s.run_hd_tools([("bad.py", []), ("good.py", [])], line_of, folder, log, "en")
            self.assertEqual([p["made"] for p in pieces], [False, True])
            self.assertEqual(pieces[0]["error"], "ValueError: zero-size array")
            self.assertIn("Traceback", log.read_text(encoding="utf-8"))  # the whole of what it said, kept
            # none failing: no log left from before
            s.run_hd_tools([("good.py", [])], line_of, folder, log, "en")
            self.assertFalse(log.exists())

    def test_each_tool_with_the_files_it_wrote(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            (folder / "one.py").write_text(
                "from pathlib import Path\n"
                "Path('mod/x').mkdir(parents=True)\n"
                "Path('mod/x/1.txt').write_text('1')\n"
                "Path('mod/shared.txt').write_text('one')\n",
                encoding="utf-8",
            )
            (folder / "two.py").write_text(
                "from pathlib import Path\nPath('mod/2.txt').write_text('2')\nPath('mod/shared.txt').write_text('two!')\n",
                encoding="utf-8",
            )

            def line_of(tool, template):
                return [sys.executable, str(folder / tool), *template]

            runs = [("one.py", []), ("two.py", [])]
            pieces = s.run_hd_tools(runs, line_of, folder, folder / "log", "en", watch=folder / "mod")
            self.assertEqual(pieces[0]["files"], ["shared.txt", "x/1.txt"])
            self.assertEqual(pieces[1]["files"], ["2.txt", "shared.txt"])  # one's file written again

    def test_the_installed_videos_are_linked_not_copied(self):
        with tempfile.TemporaryDirectory() as d:
            installed, work = Path(d) / "cinematics", Path(d) / "work" / "cinematics"
            (installed / "music").mkdir(parents=True)
            (installed / "hd802.mp4").write_bytes(b"a video")
            s.link_videos(installed, work)
            self.assertTrue(os.path.samefile(installed / "hd802.mp4", work / "hd802.mp4"))
            self.assertFalse((work / "music").exists())  # written again by the tool: never shared
            s.link_videos(Path(d) / "none", Path(d) / "work2")  # none installed yet


class HdMenuTools(unittest.TestCase):
    """Each HD menu tool kept apart: an update runs again only those that changed."""

    def tools(self, folder: Path):
        hd = folder / "tools" / "hd"
        hd.mkdir(parents=True)
        (hd / "base.py").write_text("X = 1\n", encoding="utf-8")
        (hd / "a.py").write_text("import base as b\nimport numpy\n", encoding="utf-8")
        (hd / "b.py").write_text("from base import X\n", encoding="utf-8")
        (hd / "c.py").write_text("print('c')\n", encoding="utf-8")
        return mock.patch.multiple(
            s,
            tool_folders=lambda: {"rom": folder / "tools", "hd": hd},
            HD_MENU_TOOLS=[("a.py", ["{mare}"]), ("b.py", []), ("c.py", [])],
        )

    def test_a_tool_is_made_by_what_it_imports_too(self):
        with tempfile.TemporaryDirectory() as d, self.tools(Path(d)):
            hd = Path(d) / "tools" / "hd"
            a = s.tool_made_by("a.py", ["{mare}"])
            self.assertNotEqual(a, s.tool_made_by("a.py", ["{ddd}"]))  # other arguments
            (hd / "c.py").write_text("print('changed')\n", encoding="utf-8")
            self.assertEqual(a, s.tool_made_by("a.py", ["{mare}"]))  # not imported
            (hd / "base.py").write_text("X = 2\n", encoding="utf-8")
            self.assertNotEqual(a, s.tool_made_by("a.py", ["{mare}"]))

    def test_only_what_changed_is_made_again(self):
        with tempfile.TemporaryDirectory() as d, self.tools(Path(d)):
            hd, made = Path(d) / "tools" / "hd", Path(d) / "mods" / s.HD_MOD
            files = {"a.py": ["a/1.png"], "b.py": ["b/2.png", "shared.txt"], "c.py": ["c/3.png", "shared.txt"]}
            for f in ("a/1.png", "b/2.png", "c/3.png", "shared.txt"):
                (made / f).parent.mkdir(parents=True, exist_ok=True)
                (made / f).write_text(f, encoding="utf-8")
            inputs = {"1.5+2.5": r"Z:\KH 1.5", "2.8": r"Z:\KH 2.8"}

            def record():
                tools = {t: {"by": s.tool_made_by(t, template), "files": files[t]} for t, template in s.HD_MENU_TOOLS}
                return {"steps": {"hd-menus": {"complete": True, "inputs": inputs, "tools": tools}}}

            everything = ["a.py", "b.py", "c.py"]
            state = record()
            self.assertEqual(s.menu_tools_to_make(state, inputs, made), [])
            (hd / "a.py").write_text("import base\n# changed\n", encoding="utf-8")
            self.assertEqual(s.menu_tools_to_make(state, inputs, made), ["a.py"])
            # b changed (by what it imports), and c wrote one of its files: both again, in their order
            state = record()
            (hd / "base.py").write_text("X = 3\n", encoding="utf-8")
            self.assertEqual(s.menu_tools_to_make(state, inputs, made), everything)
            state = record()
            (made / "a" / "1.png").unlink()  # one of its files gone
            self.assertEqual(s.menu_tools_to_make(state, inputs, made), ["a.py"])
            del state["steps"]["hd-menus"]["tools"]["a.py"]  # it failed last time
            self.assertEqual(s.menu_tools_to_make(state, inputs, made), ["a.py"])
            self.assertEqual(s.menu_tools_to_make(record(), {**inputs, "2.8": r"Z:\Other"}, made), everything)
            # a record from before each tool was kept: all or none
            old = {"steps": {"hd-menus": {"complete": True, "inputs": inputs, "made_by": s.made_by("hd-menus")}}}
            self.assertEqual(s.menu_tools_to_make(old, inputs, made), [])
            old["steps"]["hd-menus"]["made_by"] = "another installer's tools"
            self.assertEqual(s.menu_tools_to_make(old, inputs, made), everything)


class PortModsInTheHdPack(unittest.TestCase):
    def files(self, folder):
        return {p.relative_to(folder).as_posix(): p.read_text() for p in folder.rglob("*") if p.is_file()}

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.install = Path(self.temp.name)
        self.hd = self.install / "mods" / s.HD_MOD
        for rel, text in (("layouts/camp/en/camp.txt", "hd camp"), ("layouts/save/en/save.txt", "hd save")):
            (self.hd / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.hd / rel).write_text(text)
        self.pristine = self.files(self.hd)
        self.overhaul, self.walk = self.install / "work" / "overhaul", self.install / "work" / "walk"
        for folder, rel, text in (
            (self.overhaul, "layouts/camp/en/camp.txt", "overhaul camp"),
            (self.overhaul, "layouts/campsave/campsave.txt", "overhaul save page"),
            (self.walk, "animations/walk/ro.khw", "walk"),
            (self.walk, "days-mods/walk.txt", ""),
        ):
            (folder / rel).parent.mkdir(parents=True, exist_ok=True)
            (folder / rel).write_text(text)

    def tearDown(self):
        self.temp.cleanup()

    def test_over_the_hd_pieces_and_out_again(self):
        s.merge_port_mods(self.install, [self.overhaul, self.walk], {"mods": ["menu-overhaul", "walk"]})
        merged = self.files(self.hd)
        self.assertEqual(merged["layouts/camp/en/camp.txt"], "overhaul camp")  # the mod's over the HD menus'
        self.assertEqual(merged["layouts/save/en/save.txt"], "hd save")  # the HD menus' it has not
        self.assertEqual(merged["animations/walk/ro.khw"], "walk")
        record = s.load_state(self.install)["steps"]["port-mods"]
        self.assertEqual(record["replaced"], ["layouts/camp/en/camp.txt"])
        self.assertEqual(len(record["added"]), 3)
        s.unmerge_port_mods(self.install)
        self.assertEqual(self.files(self.hd), self.pristine)
        self.assertFalse((self.hd / "animations").exists())  # the folders the mods' files left empty
        self.assertFalse((self.install / "setup" / s.HD_ORIGINALS).exists())
        self.assertNotIn("port-mods", s.load_state(self.install).get("steps", {}))
        s.unmerge_port_mods(self.install)  # nothing in: nothing done
        self.assertEqual(self.files(self.hd), self.pristine)

    def test_a_merge_stopped_half_way(self):
        # recorded, then stopped before any HD piece was moved aside: the HD PACK's own are kept
        state = {"steps": {"port-mods": {"replaced": ["layouts/camp/en/camp.txt"], "added": ["days-mods/walk.txt"]}}}
        s.save_state(self.install, state)
        (self.hd / "days-mods").mkdir()
        (self.hd / "days-mods" / "walk.txt").write_text("")
        s.unmerge_port_mods(self.install)
        self.assertEqual(self.files(self.hd), self.pristine)

    def test_the_hd_menus_made_again_from_their_own_pieces(self):
        s.merge_port_mods(self.install, [self.overhaul], {"mods": ["menu-overhaul"]})
        recorded = {"by": "the same tool", "files": ["layouts/camp/en/camp.txt", "layouts/save/en/save.txt"]}
        state = s.load_state(self.install)
        state["steps"]["hd-menus"] = {"complete": True, "inputs": {}, "tools": {"camp.py": recorded}}
        s.save_state(self.install, state)
        with (
            mock.patch.object(s, "menu_tools_to_make", return_value=["heart_points.py"]),
            mock.patch.object(s, "check_folder", return_value=[]),
            mock.patch.object(s, "data_folder", side_effect=s.Refusal("data-missing")),
        ):
            with self.assertRaises(s.Refusal):
                s.build_hd(
                    s.argparse.Namespace(
                        install=self.install, menus=True, videos=False, c1525=Path("a"), c28=Path("b"), language="en"
                    )
                )
        self.assertEqual(self.files(self.hd), self.pristine)  # taken out before the kept pieces are copied

    def build_mods(self, **chosen):
        args = {"walk": False, "menu_overhaul": False, "kh2_prizes": False, "c1525": Path("c1525"), **chosen}
        return s.build_mods(s.argparse.Namespace(install=self.install, language="en", **args))

    def test_kh2_prizes_need_the_collection(self):
        with self.assertRaises(s.Refusal) as refused:
            self.build_mods(kh2_prizes=True, c1525=None)
        self.assertEqual(refused.exception.key, "mod-needs-1525")
        self.assertEqual(self.files(self.hd), self.pristine)

    def test_kh2_prizes_made_from_the_collection_into_the_hd_pack(self):
        import subprocess

        import collection_archive

        tools = self.install / "tools"
        for folder in ("mods", "hd"):
            (tools / folder).mkdir(parents=True)
        (tools / "decompress_data.py").write_text("")
        ran, extracted = [], []

        def tool(line, cwd, **_):  # kh2_prizes.py, stubbed: its effect and its switch
            ran.append(line)
            mod = Path(cwd) / "mods" / line[line.index("--mod") + 1]
            for rel in ("graphics/mo/prize.pak.z/fx/prize_01.txt", "days-mods/kh2-prizes.txt"):
                (mod / rel).parent.mkdir(parents=True, exist_ok=True)
                (mod / rel).write_text("kh2")
            return subprocess.CompletedProcess(line, 0, "", "")

        with (
            mock.patch.object(
                s, "tool_folders", return_value={"rom": tools, "hd": tools / "hd", "mods": tools / "mods"}
            ),
            mock.patch.object(s, "data_folder", return_value=self.install / "data"),
            mock.patch.object(collection_archive, "load_table"),
            mock.patch.object(collection_archive, "Cipher"),
            mock.patch.object(collection_archive, "Archive"),
            mock.patch.object(
                collection_archive, "materialise", side_effect=lambda a, paths, out: extracted.append(out)
            ),
            mock.patch("subprocess.run", side_effect=tool),
        ):
            result = self.build_mods(kh2_prizes=True)
        self.assertEqual(result["mods"], ["kh2-prizes"])
        self.assertEqual(len(ran), 1)
        self.assertEqual(Path(ran[0][1]).name, "kh2_prizes.py")
        self.assertEqual(Path(ran[0][ran[0].index("--kh2") + 1]).name, "kh2.hed_out")
        self.assertIn("kh2.hed_out", {Path(out).name for out in extracted})  # the collection's entries extracted
        merged = self.files(self.hd)
        self.assertEqual(merged["graphics/mo/prize.pak.z/fx/prize_01.txt"], "kh2")
        self.assertEqual(merged["days-mods/kh2-prizes.txt"], "kh2")
        self.assertEqual(merged["layouts/camp/en/camp.txt"], "hd camp")  # the HD menus' own kept
        self.assertFalse((self.install / "setup" / "mod-work").exists())
        s.unmerge_port_mods(self.install)
        self.assertEqual(self.files(self.hd), self.pristine)


class Results(unittest.TestCase):
    def run_command(self, *argv):
        with tempfile.TemporaryDirectory() as d:
            result = Path(d) / "r.json"
            code = s.main([*argv, "--result", str(result), "--language", "es"])
            return code, json.loads(result.read_text(encoding="utf-8"))

    def test_success(self):
        code, result = self.run_command("test-ok", "7")
        self.assertEqual(code, s.OK)
        self.assertEqual(result["value"], "7")
        self.assertTrue(result["ok"])

    def test_refusal(self):
        code, result = self.run_command("test-refuse")
        self.assertEqual(code, s.REFUSED)
        self.assertFalse(result["ok"])
        self.assertIn("a refusal", result["message"])
        self.assertTrue(result["message"].startswith("Algo ha fallado"))

    def test_failure(self):
        code, result = self.run_command("test-fail")
        self.assertEqual(code, s.FAILED)
        self.assertIn("broken", result["message"])
        self.assertIn("RuntimeError", result["trace"])

    def test_options_before_the_command(self):
        with tempfile.TemporaryDirectory() as d:
            result = Path(d) / "r.json"
            code = s.main(["--result", str(result), "test-ok", "1"])
            self.assertEqual(code, s.OK)
            self.assertEqual(json.loads(result.read_text(encoding="utf-8"))["value"], "1")


if __name__ == "__main__":
    unittest.main()
