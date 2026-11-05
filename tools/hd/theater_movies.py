#!/usr/bin/env python3
"""The HD videos, their subtitles and their music from the 358/2 Days theater (MARE, its videos in
--movies: found from MARE by default), from your own copy (docs/HD_SCREENS.md, "Making them"). Without
--videos, --subtitles or --music it does all three; a video already there is kept unless --force.
  python tools/hd/theater_movies.py MARE [--movies DIR] [--videos] [--subtitles] [--music] [--force]
                                         [--lang ...]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import theater_title as tt  # noqa: E402
from newgame_dialog import ctd, plain  # noqa: E402

# The theater's folders and names for the port's languages: the folder, the name in its paths,
# the exia2's language.
LANGUAGES = {
    "es": ("sp", "sp", "LANGUAGE_SP"),
    "en": ("uk", "en", "LANGUAGE_EN"),
    "fr": ("fr", "fr", "LANGUAGE_FR"),
    "de": ("gr", "de", "LANGUAGE_GE"),
    "it": ("it", "it", "LANGUAGE_IT"),
}
PATTERN = bytes.fromhex("3f1ec193264258b8aa9d5375bf629a97")  # XORed over the videos' first 0x100 bytes
SCRAMBLED = 0x100
# The tables of Days's words: groups 0x200-0x2a8 (its event scenes, whose lines some videos say
# too) and 0x391-0x3a8 (its videos'). Re:coded's (cthd500, 550, 600: groups 0x2fa-0x32f) are not
# read: two pass 64 KB, which their entries' 16-bit offsets do not reach.
WORDS = ("cthd000.ctd", "cthd050.ctd", "cthd100.ctd", "cthd300.ctd", "cthd800.ctd")
OPENING = ("DOP.mp4", "802")
ENDING = ("DED.mp4", "843")
ENDING_EVENT = "844"  # the ending's subtitles: no DS movie 844, its sound DED_MONO
OUT = tt.ROOT / "data" / "cutscenes" / "cinematics"
# the port's scene map: which of the theater's scene videos it plays
SCENE_MAP = tt.ROOT / "port" / "movies" / "hd_scenes.txt"
# Measured on the theater's window captures (hd818's lines, scaled to 1920 x 1080): the font's cells
# drawn 1.445 times their size, a space 13.75 texels, the lines 51 apart, the last one's top at 956,
# the block centred on 964 (docs/HD_SCREENS.md).
SCALE, SPACE, PITCH, LAST_TOP, CENTRE = 1.445, 13.75, 51, 956, 964
PICTURE = (1920, 1080)
RATE = 30


# --- The videos ---------------------------------------------------------------------------------


def scene_stems() -> list[str]:
    """The theater's scene videos the port plays in place of the game's own event scenes: the stems
    its scene map names (port/movies/hd_scenes.txt), in the map's order, a part of one ([from:until])
    by its whole video. A scene skipped (`skip`) names a movie's video, and a movie's entry (mv#) its
    own: the movies are all made anyway."""
    if not SCENE_MAP.is_file():
        return []
    out = []
    for raw in SCENE_MAP.read_text(encoding="utf-8").splitlines():
        line = re.sub(r"(^|\s)#.*$", "", raw).partition(";")[0]
        words = line.split()
        if len(words) >= 2 and words[1] not in ("off", "skip") and not words[0].startswith("mv#"):
            for stem in (w.partition("[")[0] for w in words[1:]):
                if stem not in out:
                    out.append(stem)
    return out


def videos(movies: Path, force: bool) -> int:
    made = 0
    names = [(p.name, p.stem[2:]) for p in sorted(movies.glob("hd8*.mp4"))] + [OPENING, ENDING]
    names += [(f"{stem}.mp4", stem[2:]) for stem in scene_stems() if not stem.startswith("hd8")]
    OUT.mkdir(parents=True, exist_ok=True)
    for name, movie in names:
        source, target = movies / name, OUT / f"hd{movie}.mp4"
        if not source.is_file():
            print(f"{source}: not found")
            continue
        if target.exists() and not force:
            print(f"{target.name}: kept (--force replaces it)")
            continue
        data = bytearray(source.read_bytes())
        if data[4:8] != b"ftyp":  # scrambled; a copy may hold a video plain already
            for i in range(min(SCRAMBLED, len(data))):
                data[i] ^= PATTERN[i % 16]
        if data[4:8] != b"ftyp":
            print(f"{source}: not one of the theater's videos as known (no MP4 header once unscrambled)")
            continue
        target.write_bytes(data)
        made += 1
        print(f"{target.name} from {name}")
    return made


# --- The subtitles ------------------------------------------------------------------------------


class EventFont:
    """The theater's event font: eventfont.tm2 (2048 x 2048, cells of 32 x 32: eventfont.inf)
    and eventfont.cod (8 bytes a glyph: its UTF-16 code, its cell's x and y, a byte, its width)."""

    def __init__(self, folder: Path):
        cod = (folder / "eventfont.cod").read_bytes()
        self.glyphs = {}
        for i in range(len(cod) // 8):
            code, x, y, _, w = struct.unpack_from("<HHHBB", cod, 8 * i)
            self.glyphs[chr(code)] = (x, y, w)
        self.picture = tt.tim2_picture((folder / "eventfont.tm2").read_bytes(), 0)
        self.ink = tt.ink_widths(self.picture, self.glyphs)  # each glyph with its whole outline
        self.missing = set()

    def line(self, text: str) -> np.ndarray:
        """The line, white and outlined, as RGBA at the picture's size."""
        width = sum(SPACE if c == " " else self.glyphs[c][2] if c in self.glyphs else 0 for c in text)
        reach = max([self.ink[c] - self.glyphs[c][2] for c in text if c in self.glyphs] + [0])
        cells = np.zeros((32, int(np.ceil(width)) + 1 + reach, 4), dtype=np.uint8)
        x = 0.0
        for c in text:
            if c == " ":
                x += SPACE
                continue
            if c not in self.glyphs:
                self.missing.add(c)
                continue
            gx, gy, w = self.glyphs[c]
            at = int(round(x))
            region = cells[:, at : at + self.ink[c]]
            glyph = self.picture[gy : gy + 32, gx : gx + self.ink[c]]
            np.copyto(region, glyph[:, : region.shape[1]], where=glyph[:, : region.shape[1], 3:4] > region[..., 3:4])
            x += w
        # the PS2's colour: the texels' grey made white (0x80 = 1.0 of a vertex colour of 2.0)
        cells[..., :3] = np.minimum(255, cells[..., :3].astype(np.int32) * 2).astype(np.uint8)
        im = Image.fromarray(cells, "RGBA")
        size = (max(1, int(round(im.size[0] * SCALE))), int(round(32 * SCALE)))
        return np.asarray(im.resize(size, Image.LANCZOS))


def schedule(event: Path, language: str):
    """The language's lines: (first frame, end frame, TextID, fade in, fade out, animated) -- its
    subtitles (SCHEDULE_CATEGORY_TEXT_MOVIE) and its animated lines (SCHEDULE_CATEGORY_TEXT_ANIM: a
    voice over the scene, Xion's in hd837 and hd840, coming over InFrame frames and going over
    OutFrame, set higher by the empty rows its text ends with)."""
    xml = event.read_bytes().decode("utf-8", errors="replace")
    out = []
    for kind in ("TEXT_MOVIE", "TEXT_ANIM"):
        for category in re.finditer(
            rf'<SCHEDULE_CATEGORY type = "SCHEDULE_CATEGORY_{kind}">(.*?)</SCHEDULE_CATEGORY>', xml, re.S
        ):
            if f'Language="{language}"' not in category.group(1):  # (the file's header names a language too)
                continue
            for data in re.findall(r"<DATA ([^>]*)/>", category.group(1)):
                a = dict(re.findall(r'(\w+)="([^"]*)"', data))
                if "TextID" in a:
                    out.append(
                        (
                            int(a["FrameStart"]),
                            int(a["FrameEnd"]),
                            int(a["TextID"], 16),
                            int(a.get("InFrame", "0")),
                            int(a.get("OutFrame", "0")),
                            kind == "TEXT_ANIM",
                        )
                    )
            break
    return sorted(out)


def subtitles(mare: Path, lang: str) -> int:
    folder, name, language = LANGUAGES[lang]
    root = mare / folder
    words = {}
    for table in WORDS:
        words.update(ctd(root / "message" / name / "event" / "hd" / "bin" / table))
    font = EventFont(root / "font")
    out = OUT / "subtitles" / lang
    out.mkdir(parents=True, exist_ok=True)
    made = 0
    scenes = set(scene_stems())
    events = sorted((root / "event" / name / "hd").glob("hd*/hd*.exia2"))
    for event in [e for e in events if e.stem.startswith("hd8") or e.stem in scenes]:
        movie = event.stem[2:]
        if movie == ENDING_EVENT:
            movie = ENDING[1]
        lines = schedule(event, language)
        if not lines:
            continue
        atlas, placed = tt.Atlas(), []
        for n, (first, end, text_id, fade_in, fade_out, animated) in enumerate(lines):
            text = words.get((text_id >> 16, text_id & 0xFFFF))
            if text is None:
                print(f"{lang} hd{movie}: no words for {text_id:x}")
                continue
            rows = [plain(r).rstrip() for r in text.replace("\r", "").split("\n")]
            if not animated:
                rows = [r for r in rows if r]  # (an animated line's empty rows set it higher: kept)
            images = [font.line(r) if r else None for r in rows]
            if not any(image is not None for image in images):
                continue
            block = max(image.shape[1] for image in images if image is not None)
            for i, image in enumerate(images):
                if image is None:
                    continue
                atlas.add((n, i), image)
                y = LAST_TOP - (len(rows) - 1 - i) * PITCH
                placed.append((first, end, (n, i), round(CENTRE - block / 2), y, fade_in, fade_out))
        if not placed:
            print(f"{lang} hd{movie}: no lines")
            continue
        pixels = atlas.pack()
        Image.fromarray(pixels, "RGBA").save(out / f"hd{movie}.png")
        txt = [
            f"# hd{movie}.mp4's subtitles as the HD collection's 358/2 Days theater draws them, made by",
            "# tools/hd/theater_movies.py from the player's own copy: each line's frames (the video's,",
            "# `rate` a second; it shows from the first to before the end), its place in the picture and",
            "# its texels in the atlas; an animated line's, the frames it takes to come and to go.",
            f"picture {PICTURE[0]} {PICTURE[1]}",
            f"rate {RATE}",
            f"atlas hd{movie}.png",
        ]
        for first, end, region, x, y, fade_in, fade_out in placed:
            u, v = atlas.at[region]
            h, w = atlas.pixels[region].shape[:2]
            fades = f" {fade_in} {fade_out}" if fade_in or fade_out else ""
            txt.append(f"line {first} {end} {x} {y} {u} {v} {w} {h}{fades}")
        (out / f"hd{movie}.txt").write_bytes(("\n".join(txt) + "\n").encode("utf-8"))
        made += 1
    if font.missing:
        print(f"{lang}: the event font has no {''.join(sorted(font.missing))!r} (left out)")
    print(f"{lang}: {made} videos' subtitles in {out}")
    return made


# --- The music ----------------------------------------------------------------------------------

# The theater plays most videos with music of its own over their sound (their sound has the voices
# and effects only): each event file's BGM schedule names the track (sound/<track>.win32.scd), the
# frames it plays between and the volume's ramps. The schedule is the same in every language.
MUSIC_EVENTS = ("uk", "en")  # the event files read for it (any language's would do)
OGG = 6  # an SCD sound's codec: Ogg Vorbis


def scd_ogg(data: bytes) -> tuple[bytes, int, int]:
    """An SCD's first sound as a plain Ogg Vorbis file, the sample its loop goes back to (-1: it
    plays once, its loop's end 0) and its rate.
    The SCD's tables start at its header's size (u16 at 0x0e); the fourth word of the tables'
    header points to the sounds' offsets; a sound's header (0x20 bytes) gives its stream's size, its
    channels, rate, codec, loop start and end (bytes into the stream: a page's start) and the size of
    what comes before the stream; for Ogg, that is a header (0x20 bytes: its version, its size, the
    byte the Vorbis header pages are XORed with; at 0x10 the seek table's size, then the Vorbis
    header's), the seek table and the Vorbis header pages, then the stream's own pages."""
    if data[:8] != b"SEDBSSCF":
        raise ValueError("not an SCD")
    tables = struct.unpack_from("<H", data, 0x0E)[0]
    sounds = struct.unpack_from("<I", data, tables + 0x0C)[0]
    entry = struct.unpack_from("<I", data, sounds)[0]
    size, _channels, rate, codec, loop_start, loop_end, extra, _aux = struct.unpack_from("<8i", data, entry)
    if codec != OGG:
        raise ValueError(f"codec {codec}, not Ogg Vorbis")
    head = entry + 0x20
    header_size, key = data[head + 1], data[head + 2]
    seek_size, vorbis_size = struct.unpack_from("<II", data, head + 0x10)
    vorbis = head + header_size + seek_size
    pages = bytes(b ^ key for b in data[vorbis : vorbis + vorbis_size])
    stream = data[head + extra : head + extra + size]
    if pages[:4] != b"OggS" or stream[:4] != b"OggS":
        raise ValueError("no Ogg pages where the header says")
    # the loop's sample: the granule of the page before the one the loop starts at
    loop, at, granule = (0 if loop_end > 0 else -1), 0, 0
    while loop >= 0 and at < len(stream) and stream[at : at + 4] == b"OggS":
        if at == loop_start:
            loop = granule
            break
        granule = struct.unpack_from("<q", stream, at + 6)[0]
        count = stream[at + 26]
        at += 27 + count + sum(stream[at + 27 : at + 27 + count])
    return pages + stream, loop, rate


def bgm_schedule(xml: str) -> dict | None:
    """An event file's BGM schedule: where the music comes from and goes ("field": the video before
    or after it plays it on; "stop"), its cues (first frame, end frame, track) and its volume points
    (first frame, end frame, percent: a ramp from the level before). None without cues."""
    found = re.search(r'<SCHEDULE_CATEGORY type = "SCHEDULE_CATEGORY_BGM">(.*?)</SCHEDULE_CATEGORY>', xml, re.S)
    if not found:
        return None
    block = found.group(1)
    param = dict(re.findall(r'(\w+)="([^"]*)"', (re.search(r"<PARAM ([^/]*)/>", block) or [""])[0]))

    def data(tag: str) -> list[dict]:
        part = re.search(rf"<{tag}>(.*?)</{tag}>", block, re.S)
        return (
            [dict(re.findall(r'(\w+)="([^"]*)"', d)) for d in re.findall(r"<DATA ([^/]*)/>", part.group(1))]
            if part
            else []
        )

    cues = [(int(d["FrameStart"]), int(d["FrameEnd"]), d["SoundName"]) for d in data("BGM1")]
    if not cues:
        return None
    return {
        "from": "field" if param.get("BgmStartSet", "STOP").startswith("FIELD") else "stop",
        "to": "field" if param.get("BgmEndSet", "STOP").startswith("FIELD") else "stop",
        "cues": cues,
        "volume": [(int(d["FrameStart"]), int(d["FrameEnd"]), int(d["Param"])) for d in data("VOLUME1")],
    }


def music(mare: Path) -> int:
    folder, name = MUSIC_EVENTS
    out = OUT / "music"
    out.mkdir(parents=True, exist_ok=True)
    scenes = set(scene_stems())
    tracks: dict[str, int] = {}
    made = 0
    for event in sorted((mare / folder / "event" / name / "hd").glob("hd*/hd*.exia2")):
        if not (event.stem.startswith("hd8") or event.stem in scenes):
            continue
        movie = event.stem[2:]
        if movie == ENDING_EVENT:
            movie = ENDING[1]
        plan = bgm_schedule(event.read_bytes().decode("utf-8", errors="replace"))
        if plan is None:
            continue
        for _, _, track in plan["cues"]:
            if track not in tracks:
                ogg, loop, rate = scd_ogg((mare / "sound" / f"{track}.win32.scd").read_bytes())
                (out / f"{track}.ogg").write_bytes(ogg)
                tracks[track] = loop
                print(f"{track}.ogg: {len(ogg)} bytes, {rate} Hz, its loop from sample {loop}")
        txt = [
            f"# hd{movie}.mp4's music as the HD collection's 358/2 Days theater plays it over the video's",
            "# sound, made by tools/hd/theater_movies.py from the player's own copy. The video's frames",
            "# (`rate` a second); `from`/`to`: whether the music comes from the video before and goes on",
            "# into the next (field) or not (stop); each cue plays music/<track>.ogg from its start, from",
            "# its first frame to before its end, looping back to sample `loop` at the track's end (-1:",
            "# it plays once); each volume point ramps the music to `level` percent between its two",
            "# frames (from 100).",
            f"rate {RATE}",
            f"from {plan['from']}",
            f"to {plan['to']}",
        ]
        txt += [f"cue {first} {end} {track} {tracks[track]}" for first, end, track in plan["cues"]]
        txt += [f"volume {first} {end} {level}" for first, end, level in plan["volume"]]
        (out / f"hd{movie}.txt").write_bytes(("\n".join(txt) + "\n").encode("utf-8"))
        made += 1
    print(f"{made} videos' music in {out} ({len(tracks)} tracks)")
    return made


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("mare", type=Path, help="the theater extracted: <collection>/Image/Mare.hed_out/original")
    ap.add_argument(
        "--movies",
        type=Path,
        help="its videos: <collection>/STEAM/Mare/MOVIE/Days/dt (Steam's), <collection>/EPIC/Mare/MOVIE/Days/en "
        "(the Epic Games Store's)",
    )
    ap.add_argument("--videos", action="store_true")
    ap.add_argument("--subtitles", action="store_true")
    ap.add_argument("--music", action="store_true")
    ap.add_argument("--force", action="store_true", help="replace the videos already there")
    ap.add_argument("--lang", nargs="*", default=list(LANGUAGES), choices=list(LANGUAGES))
    args = ap.parse_args()
    both = not args.videos and not args.subtitles and not args.music
    if args.videos or both:
        collection = args.mare.resolve().parents[2]
        movies = args.movies or next(
            (
                m
                for m in (
                    collection / "STEAM" / "Mare" / "MOVIE" / "Days" / "dt",
                    collection / "EPIC" / "Mare" / "MOVIE" / "Days" / "en",
                )
                if m.is_dir()
            ),
            collection / "STEAM" / "Mare" / "MOVIE" / "Days" / "dt",
        )
        print(videos(movies, args.force), "videos made")
    if args.subtitles or both:
        for lang in args.lang:
            subtitles(args.mare, lang)
    if args.music or both:
        music(args.mare)
    return 0


if __name__ == "__main__":
    sys.exit(main())
