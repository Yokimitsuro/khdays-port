"""The Walk mod's clips (animations/walk/<code>.khw, docs/WALK_MOD.md) made from your own copy of the game: each
Organization member walks with the game's own walk, its weapon held as the game holds it.

    python tools/mods/walk_clips.py [--data data/extracted/<hash>] [--mod "Days - Walk"] [codes...]

writes mods/<mod>/animations/walk/ and the mod's switch, mods/<mod>/days-mods/walk.txt (runtime/mods/installed.h).

The walk is the character's own event walk (e_xxx_908), or Roxas's retargeted by the Bip01 names for those who
have none: the body and the arms are the walk's own, the hips at the character's height, the feet on the ground
the character's idle stands on. Over it only what the game itself shows when the character stands with the weapon
out (b_xx_A000, the idle):
- the hand that holds a weapon takes the idle's wrist (the hand joint's own rotation), so the weapon is gripped
  as the game grips it; the arm swings as the walk swings it;
- the characters in IDLE_ARMS hold the whole arm as the idle does (the clavicle anchored to the chest, a little of
  the walk's swing on the upper arm): those whose weapon the idle carries in a way a walking arm would not (a
  claymore, a scythe, the arrowguns, the chakrams, the shield); Axel's right arm and chakram the mirror image of
  his left (MIRROR_ARMS);
- Roxas's, Xion's and the two-Keyblade Roxas's Keyblades (KEYBLADE_FORWARD) point forward and a little down
  instead of out to the side, the same on every frame: the holding arm swings less (HOLDING_SWING) and the wrist
  keeps the blade there (the two-Keyblade Roxas's left blade along FORWARD's mirror image). Roxas's unarmed walk
  (the story's field) is ro.khw, his armed one ro_k.khw. Their keychain hangs from the pommel (KEYCHAIN: a
  pendulum, written as the clip's HANG trailer);
- every weapon joint rides in its hand as the idle holds it; the three whose weapon is a model of its own placed
  by the game rather than a joint (Zexion's book, Luxord's cards, Xaldin's lances) get a second clip,
  <code>_w.khw, the holding hand(s) frame by frame and as the idle holds them (walk.c moves the weapon with it);
- Xemnas (his run is a float), Demyx and the characters who are not of the Organization have no clip: the game's
  own run, slowed, as without the mod (NO_WALK).
Every character's cycle lasts as long as Roxas's (CYCLE_SECONDS); each clip ends with the frames its feet are
planted (STEP), where the port plays the footstep.

A clip (walk.c's reader): "KHW1", the frame and joint counts and the frames a second (u32, u32, f32), each frame's
joints' (quaternion x y z w, translation) in the NNS row-vector layout (f32), then its trailers: "STEP", a count
and that many frames (u32); "HANG", the weapon file's folder (4 bytes: ba/ch/<folder>/w_.p2), the keychain's
node, the frame count and the node's rotation on every frame (4 f32).
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nitro3d as n3  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CODES = "ax de do go la le lu ma mi r2 ri ro sa so ve xa xe xi xo ze".split()
# The characters with a walk loop of their own among the event motions: (event model, the motion's file, its
# entry or None for the model's own file, the motion's name); the others take Roxas's
NATIVE = {
    "ro": ("mi/ch/0C.z", "mi/mo/ro.p2", 1, "e_rox_908a"),
    "xo": ("mi/ch/0D.z", "mi/mo/xo.p2", 1, "e_xio_908a"),
    "le": ("mi/ch/04.z", "mi/ch/04.z", None, "e_lex_908"),
    "ze": ("mi/ch/05.z", "mi/ch/05.z", None, "e_zex_908"),
    "ve": ("mi/ch/03.z", "mi/ch/03.z", None, "e_vex_908"),
    "ma": ("mi/ch/0A.z", "mi/ch/0A.z", None, "e_mar_908"),
}
ARM = ("Clavicle", "UpperArm", "Forearm", "Hand", "Finger")
CYCLE_SECONDS = 37 / 42.31  # Roxas's cycle: 37 frames at the pace his walk keeps its feet on the ground
IDLE_ARMS = {"sa", "ma", "xi", "ax", "le"}  # the whole holding arm as the idle (see above)
IDLE_SWING = 0.2  # of the walk's upper-arm swing kept on an idle arm
MIRROR_ARMS = {"ax": {}}  # the right arm the left's mirror image; turns in toward the body by bone (degrees): none
KEYBLADE_FORWARD = {"ro", "xo", "r2"}
# where the blade points, in the model root's axes (x left, y up, z forward): forward, about 27 degrees down, a
# little out from the right leg; the left hand's blade the mirror image
FORWARD = np.array([-0.12, -0.55, 1.0])
LEFT_FORWARD = FORWARD * np.array([-1.0, 1.0, 1.0])
STEADY = 1.0  # of each frame's remaining turn toward FORWARD the wrist takes
HOLDING_SWING = 0.6  # of the walk's swing the arm that holds a Keyblade keeps (the other one swings in full)
NO_WALK = {"xe", "de", "so", "do", "go", "mi", "ri"}
# The Keyblade's keychain: node 1 of the Keyblade models in ba/ch/<folder>/w_.p2 (Roxas's file is Xion's too; the
# two-Keyblade Roxas's has one Keyblade model drawn in both hands), at the pommel, its shapes hanging along the
# node's -z. A damped pendulum from the pommel, driven by the walk's own hand, gives its direction on every frame
KEYCHAIN = {"ro": ("ro", 1), "xo": ("ro", 1), "r2": ("r2", 1)}  # code: (the weapon file's folder, the node)
KEYCHAIN_PIVOT = np.array([0.0, 0.0, -0.17])  # the pommel, in the weapon's axes
KEYCHAIN_LENGTH = 0.3  # to the token's middle
HEIGHT_M = {"ro": 1.55, "xo": 1.5, "r2": 1.55}  # the character's height in metres: gravity in the model's units
SETTLE = 0.15  # seconds the pendulum's swing (about the pommel) takes to fall to a third
DRIVE = 0.3  # of the pommel's own sway that moves the pendulum
# The hand(s) holding a weapon the game places as a model of its own: the idle's wrist, and the weapon clip's
# hands in this order (Xaldin: the lance of xa_w_L in the left, of xa_w_R01 in the right)
WEAPON_HAND = {"de": "L", "ze": "R", "lu": "R", "xa": "LR"}


# ---- The game's motions -------------------------------------------------------------------------------------


class Game:
    """The game's files (its NitroFS)."""

    def __init__(self, nitrofs: Path):
        self.nitrofs = nitrofs

    def event_model(self, path: str) -> list[dict]:
        blob = n3.read(self.nitrofs, path)
        return n3.models(blob, blob.find(b"BMD0"))[0][1]

    def motion(self, path: str, entry: int | None, name: str) -> n3.Animation:
        blob = n3.read(self.nitrofs, path)
        if entry is not None:
            blob = n3.p2_entries(blob)[entry]
        for an in n3.animations(blob):
            if an.name == name:
                return an
        raise KeyError(f"{path}: no motion {name}")

    def field(self, code: str):
        """The character's field model: its nodes' names, parents and rest locals, its run and its idle."""
        names, parents, rest = n3.skeleton(self.nitrofs, code)
        anims = n3.animations(n3.read(self.nitrofs, f"ba/ch/{code}/def.p.z"))
        run = next(a for a in anims if a.name.endswith("_A001"))
        idle = next(a for a in anims if a.name.endswith("_A000"))
        return names, parents, rest, run, idle


def local_pose(an: n3.Animation, frame: int, rest_r, rest_t):
    out = []
    for i, node in enumerate(an.nodes):
        r, t = n3.node_local(an, node, frame)
        out.append((np.array(rest_r[i] if r is None else r, float), np.array(rest_t[i] if t is None else t, float)))
    return out


def quat(m) -> np.ndarray:
    """A row-vector rotation's quaternion as walk.c's quat_from_matrix makes it."""
    m = np.asarray(m, float).reshape(9)
    tr = m[0] + m[4] + m[8]
    if tr > 0:
        s = np.sqrt(tr + 1) * 2
        q = [(m[5] - m[7]) / s, (m[6] - m[2]) / s, (m[1] - m[3]) / s, s / 4]
    elif m[0] > m[4] and m[0] > m[8]:
        s = np.sqrt(1 + m[0] - m[4] - m[8]) * 2
        q = [s / 4, (m[3] + m[1]) / s, (m[6] + m[2]) / s, (m[5] - m[7]) / s]
    elif m[4] > m[8]:
        s = np.sqrt(1 + m[4] - m[0] - m[8]) * 2
        q = [(m[3] + m[1]) / s, s / 4, (m[7] + m[5]) / s, (m[6] - m[2]) / s]
    else:
        s = np.sqrt(1 + m[8] - m[0] - m[4]) * 2
        q = [(m[6] + m[2]) / s, (m[7] + m[5]) / s, s / 4, (m[1] - m[3]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def mat(q) -> np.ndarray:
    """A quaternion's row-vector rotation, as walk.c's rotation[] makes it."""
    x, y, z, w = q
    return np.array(
        [
            1 - 2 * (y * y + z * z),
            2 * (x * y + z * w),
            2 * (x * z - y * w),
            2 * (x * y - z * w),
            1 - 2 * (x * x + z * z),
            2 * (y * z + x * w),
            2 * (x * z + y * w),
            2 * (y * z - x * w),
            1 - 2 * (x * x + y * y),
        ]
    ).reshape(3, 3)


def world(pose, parents):
    out = [None] * len(pose)
    for i, (r, t) in enumerate(pose):
        local = np.eye(4)
        local[:3, :3] = r
        local[3, :3] = t
        out[i] = local if parents[i] < 0 else local @ out[parents[i]]
    return out


def clip_world(d, f, parents):
    return world([(mat(d[f, j, :4]), d[f, j, 4:]) for j in range(d.shape[1])], parents)


def lowest_foot(worlds, names) -> float:
    feet = (names.index("Bip01_L_Foot"), names.index("Bip01_R_Foot"))
    return float(min(w[k][3, 1] for w in worlds for k in feet))


def event_walk(game: Game, code: str):
    """The event walk on the character's field skeleton: its own (or Roxas's) by the Bip01 names, the hips at the
    character's height, the joints outside the biped at the run's average pose, standing on the ground of the
    character's own idle. (frames, joints, 7) and its source's frame count."""
    names, parents, rest, run, idle = game.field(code)
    model, path, entry, name = NATIVE[code if code in NATIVE else "ro"]
    event = game.event_model(model)
    an = game.motion(path, entry, name)
    assert len(an.nodes) == len(event), (code, len(an.nodes), len(event))
    en = [n["name"] for n in event]
    er = [n["R"] for n in event]
    et = [n["T"] for n in event]
    first, last = local_pose(an, 0, er, et), local_pose(an, an.frames - 1, er, et)
    loop_repeats = max(np.abs(a[0] - b[0]).max() for a, b in zip(first, last)) < 0.02
    frames = an.frames - 1 if loop_repeats else an.frames
    src = [local_pose(an, f, er, et) for f in range(frames)]

    def leg(ts, nm):
        return sum(np.linalg.norm(ts[nm.index(b)]) for b in ("Bip01_L_Calf", "Bip01_L_Foot"))

    rest_r, rest_t = [r[:3, :3] for r in rest], [r[3, :3] for r in rest]
    ratio = leg(rest_t, names) / leg(et, en)
    runp = [local_pose(run, f, rest_r, rest_t) for f in range(run.frames)]
    held = []  # the run's average local of each joint
    for j in range(len(names)):
        rs = [runp[f][j][0] for f in range(run.frames)]
        held.append((n3.mean_rotation([r.T for r in rs]).T, np.mean([runp[f][j][1] for f in range(run.frames)], 0)))
    data = np.zeros((frames, len(names), 7), np.float32)
    for f in range(frames):
        for j, n in enumerate(names):
            if n.startswith("Bip01_R_") and not any(k in n for k in ("Thigh", "Calf", "Foot", "Toe")):
                r, t = held[j]  # (the right arm: walk_build's own below)
            elif (n.startswith("Bip01_") or n == "Bip01") and n in en:
                r, t = src[f][en.index(n)]
                t = t * ratio if n == "Bip01" else rest[j][3, :3]
            else:
                r, t = held[j]
            data[f, j, :4] = quat(r)
            data[f, j, 4:] = t
        if f:
            for j in range(len(names)):
                if np.dot(data[f, j, :4], data[f - 1, j, :4]) < 0:
                    data[f, j, :4] *= -1
    idle_worlds = [world(local_pose(idle, f, rest_r, rest_t), parents) for f in range(idle.frames)]
    ground = min(lowest_foot(idle_worlds, names), lowest_foot([world(p, parents) for p in runp], names))
    walk_low = lowest_foot([clip_world(data, f, parents) for f in range(frames)], names)
    data[:, names.index("Bip01"), 5] += ground - walk_low
    return data, (names, parents, rest, run, idle), (en, src)


# ---- The weapons and the arms -------------------------------------------------------------------------------


def qmean(qs):
    """The average rotation of quaternions (the eigenvector of their outer products' sum)."""
    m = sum(np.outer(q, q) for q in qs)
    return np.linalg.eigh(m)[1][:, -1]


def slerp(a, b, t):
    if np.dot(a, b) < 0:
        b = -b
    d = float(np.clip(np.dot(a, b), -1, 1))
    th = np.arccos(d)
    q = a * (1 - t) + b * t if th < 1e-6 else (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)
    return q / np.linalg.norm(q)


def turn(a, b, share=1.0):
    """The rotation (row vectors: v @ Q) turning direction `a` toward `b` by the least angle, `share` of the way."""
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    v = np.cross(a, b)
    s = float(np.linalg.norm(v))
    if s < 1e-9:
        return np.eye(3)
    k = v / s
    angle = np.arctan2(s, float(np.dot(a, b))) * share
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return (np.eye(3) + np.sin(angle) * kx + (1 - np.cos(angle)) * kx @ kx).T


def about(axis, degrees):
    """The rotation (row vectors) by `degrees` about `axis`."""
    k = axis / np.linalg.norm(axis)
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    a = np.radians(degrees)
    return (np.eye(3) + np.sin(a) * kx + (1 - np.cos(a)) * kx @ kx).T


def arm_side(name):
    for side in ("L", "R"):
        if name.startswith(f"Bip01_{side}_") and any(k in name for k in ARM):
            return side
    return None


def blade_forward(d, names, parents, hand, weapon, grip, forward=FORWARD):
    """The hand turned at the wrist so that the weapon's blade (its +z) points along `forward`: one turn for all the
    frames, the least that brings the blade there on the average frame (the wrist as near the walk's own as it
    can), then STEADY of what is left on each frame, and the blade's flat side upright. Returns the first turn's
    angle and the lowest the blade's tip goes."""
    fore = parents[hand]
    u = grip[weapon][2, :3]  # the blade in the hand's axes
    worlds = [clip_world(d, f, parents) for f in range(d.shape[0])]
    hm = mat(qmean([quat(w[hand][:3, :3]) for w in worlds]))
    q = turn(u @ hm, forward)
    k = hm @ q @ hm.T  # the same turn, in the hand's own axes
    lowest = 9.0
    for f, w in enumerate(worlds):
        fw = w[fore][:3, :3]
        h = k @ mat(d[f, hand, :4]) @ fw
        h = h @ turn(u @ h, forward, STEADY)
        # upright about its own length: the blade's thin axis (the weapon's x) level, by the least turn about the
        # blade, which leaves where it points as it is
        blade = u @ h
        thin = grip[weapon][0, :3] @ h
        side = np.cross(blade / np.linalg.norm(blade), thin)
        roll = np.degrees(np.arctan2(-thin[1], side[1]))
        if roll > 90:
            roll -= 180
        elif roll < -90:
            roll += 180
        h = h @ about(blade, roll)
        d[f, hand, :4] = quat(h @ fw.T)
        tip = (grip[weapon][3, :3] + 0.83 * u) @ h + (d[f, hand, 4:] @ fw + w[fore][3, :3])
        lowest = min(lowest, float(tip[1]))
    return float(np.degrees(np.arccos(np.clip((np.trace(q) - 1) / 2, -1, 1)))), lowest


def keychain(d, names, parents, weapon, fps, height_m):
    """The keychain's rotation on every frame (quaternions, its node's local in the weapon's axes): a damped
    pendulum hanging from the pommel, the pommel carried by the walk, run until its swing repeats with the cycle.
    Returns them and the most the chain leaves straight down (degrees)."""
    frames = d.shape[0]
    worlds = [clip_world(d, f, parents)[weapon] for f in range(frames)]
    first = clip_world(d, 0, parents)
    head = max(first[j][3, 1] for j, n in enumerate(names) if n == "Bip01_Head")
    gravity = np.array([0.0, -9.8 * (head + 0.12) / height_m, 0.0])  # (the head bone sits ~0.12 below the top)
    pivots = [KEYCHAIN_PIVOT @ w[:3, :3] + w[3, :3] for w in worlds]
    middle = np.mean(pivots, 0)
    pivots = [middle + (p - middle) * DRIVE for p in pivots]
    sub = 8
    dt = 1.0 / (fps * sub)
    keep = np.exp(-dt / (SETTLE / np.log(3)))
    end = pivots[0] + np.array([0.0, -KEYCHAIN_LENGTH, 0.0])
    last = end.copy()
    pivot = pivots[0]
    dirs = [None] * frames
    for _ in range(16):
        for f in range(frames):
            a, b = pivots[f], pivots[(f + 1) % frames]
            for s in range(sub):
                was, pivot = pivot, a + (b - a) * (s + 1) / sub
                # the chain carried by the pommel; only its swing about the pommel dies down
                swing = (end - last) - (pivot - was)
                step = end + (pivot - was) + swing * keep + gravity * dt * dt
                last = end
                v = step - pivot
                end = pivot + v / np.linalg.norm(v) * KEYCHAIN_LENGTH
            dirs[(f + 1) % frames] = (end - b) / KEYCHAIN_LENGTH
    quats, most = np.zeros((frames, 4), "<f4"), 0.0
    for f in range(frames):
        most = max(most, float(np.degrees(np.arccos(np.clip(-dirs[f][1], -1, 1)))))
        local = dirs[f] @ worlds[f][:3, :3].T  # in the weapon's axes (its root node, node 0, stays put)
        quats[f] = quat(turn(np.array([0.0, 0.0, -1.0]), local))
    for f in range(1, frames):
        if np.dot(quats[f], quats[f - 1]) < 0:
            quats[f] *= -1
    return quats, most


def damp_swing(d, names, side, share):
    """The side's arm (clavicle, upper arm, forearm) swinging `share` of the walk's swing about its average."""
    for j, n in enumerate(names):
        if arm_side(n) == side and ("Clavicle" in n or "UpperArm" in n or "Forearm" in n):
            mean = qmean([d[f, j, :4] for f in range(d.shape[0])])
            for f in range(d.shape[0]):
                d[f, j, :4] = slerp(mean, d[f, j, :4], share)


def mirrored_arms(names, parents, rest, pose, turns):
    """`pose` (local) with the left arm's bones turned in toward the body by `turns` ({bone: degrees}, about the
    chest's forward axis) and the right arm made its mirror image across the chest's middle plane. Left and right
    bones' own axes are not mirror images of each other in a biped: each right bone takes the left's place in the
    chest's axes reflected, corrected by how the two bones' rest axes differ."""
    chest = names.index("Bip01_Spine1")
    hand = names.index("Bip01_L_Hand")

    def turned(p, part, degrees):
        j = names.index(f"Bip01_L_{part}")
        w = world(p, parents)
        c = w[chest][:3, :3]
        ahead = np.array([0.0, 0.0, 1.0]) @ c.T
        rel, rel_parent = w[j][:3, :3] @ c.T, w[parents[j]][:3, :3] @ c.T
        out = list(p)
        out[j] = (rel @ about(ahead, degrees) @ rel_parent.T, p[j][1])
        return out

    # inward: the sense that brings the hand (hanging below the shoulder) nearer the middle
    sign = 1 if world(turned(pose, "UpperArm", 5), parents)[hand][3, 0] < world(pose, parents)[hand][3, 0] else -1
    p = list(pose)
    for part in ("UpperArm", "Forearm", "Hand"):
        if turns.get(part):
            p = turned(p, part, sign * turns[part])
    best = world(p, parents)[hand][3, 0]
    r = world([(m[:3, :3], m[3, :3]) for m in rest], parents)
    rc = r[chest][:3, :3]
    lateral = (r[names.index("Bip01_L_Clavicle")][3, :3] - r[names.index("Bip01_R_Clavicle")][3, :3]) @ rc.T
    lateral /= np.linalg.norm(lateral)
    s = np.eye(3) - 2 * np.outer(lateral, lateral)
    w = world(p, parents)
    c = w[chest][:3, :3]
    rel_right, flips = {}, {}
    for jl, n in enumerate(names):
        if arm_side(n) != "L":
            continue
        jr = names.index(n.replace("Bip01_L_", "Bip01_R_"))
        b = (r[jr][:3, :3] @ rc.T) @ s @ (r[jl][:3, :3] @ rc.T).T
        flips[jr] = b
        rel_right[jr] = b @ (w[jl][:3, :3] @ c.T) @ s
        parent = parents[jr]
        rel_p = rel_right[parent] if parent in rel_right else w[parent][:3, :3] @ c.T
        p[jr] = (rel_right[jr] @ rel_p.T, p[jr][1])
    return p, float(best), flips[names.index("Bip01_R_Hand")]


def mirrored_grip(grip_left, hand_flip):
    """The right hand's hold, the left's mirror image (a flat chakram, symmetric in its x), its own x turned over
    so that it stays a rotation."""
    g = np.eye(4)
    g[:3, :3] = np.diag([-1.0, 1.0, 1.0]) @ grip_left[:3, :3] @ hand_flip.T
    g[3, :3] = grip_left[3, :3] @ hand_flip.T
    return g


def weapons(names, parents, run_world):
    """The weapon joints and the hand each is held by: a child of a hand, or a child of the root named for a side
    (<x>_h_L, <x>_h_R) or, its name saying none, the hand nearest it in `run_world`."""
    out = {}
    hands = [names.index("Bip01_L_Hand"), names.index("Bip01_R_Hand")]
    for j, n in enumerate(names):
        if n.startswith("Bip01"):
            continue
        p = names[parents[j]] if parents[j] >= 0 else ""
        if p in ("Bip01_L_Hand", "Bip01_R_Hand"):
            out[j] = names.index(p)
        elif parents[j] == 0 and ("_h_" in n or "_w_" in n):
            if n[-2:] in ("_L", "_R"):
                out[j] = names.index(f"Bip01_{n[-1]}_Hand")
            else:
                out[j] = min(hands, key=lambda h: np.linalg.norm(run_world[j][3, :3] - run_world[h][3, :3]))
    return out


def mean_pose(frames):
    """A motion's average local pose: each joint's average rotation and offset."""
    return [
        (mat(qmean([quat(f[j][0]) for f in frames])), np.mean([f[j][1] for f in frames], 0))
        for j in range(len(frames[0]))
    ]


def in_hand(poses, parents, held):
    """Each weapon's place in its hand over `poses` (local poses): a 4x4 (row vectors), averaged."""
    rel = {}
    for w, h in held.items():
        qs, ts = [], []
        for p in poses:
            wp = world(p, parents)
            m = wp[w] @ np.linalg.inv(wp[h])
            qs.append(quat(m[:3, :3]))
            ts.append(m[3, :3])
        r = np.eye(4)
        r[:3, :3] = mat(qmean(qs))
        r[3, :3] = np.mean(ts, 0)
        rel[w] = r
    return rel


def place_weapons(d, names, parents, held, grip):
    """Each weapon in its hand as `grip` holds it, frame by frame; its local written into `d`."""
    for f in range(d.shape[0]):
        w = clip_world(d, f, parents)
        for wj, h in held.items():
            m = grip[wj] @ w[h]
            local = m @ np.linalg.inv(w[parents[wj]]) if parents[wj] >= 0 else m
            d[f, wj, :4] = quat(local[:3, :3])
            d[f, wj, 4:] = local[3, :3]
            w[wj] = m


def idle_arm(d, names, parents, side, idle_mean, swing):
    """The side's arm as `idle_mean` holds it: the clavicle keeps that pose's turn against the chest (Bip01_Spine1's
    world: the event walk's spine and the field rig's are not the same bones' locals), the rest of the arm its
    locals, the upper arm with `swing` of the walk's own swing."""
    chest = names.index("Bip01_Spine1")
    clav = names.index(f"Bip01_{side}_Clavicle")
    upper = names.index(f"Bip01_{side}_UpperArm")
    wi = world(idle_mean, parents)
    against_chest = wi[clav][:3, :3] @ np.linalg.inv(wi[chest][:3, :3])
    mean_upper = qmean([d[f, upper, :4] for f in range(d.shape[0])])
    for f in range(d.shape[0]):
        w = clip_world(d, f, parents)
        want = against_chest @ w[chest][:3, :3]
        d[f, clav, :4] = quat(want @ np.linalg.inv(w[parents[clav]][:3, :3]))
        d[f, clav, 4:] = idle_mean[clav][1]
        for j, n in enumerate(names):
            if arm_side(n) != side or j == clav:
                continue
            q_idle = quat(idle_mean[j][0])
            if j == upper and swing > 0:
                dq = quat(mat(d[f, j, :4]) @ mat(mean_upper).T)
                dq = slerp(np.array([0.0, 0.0, 0.0, 1.0]), dq, swing)
                d[f, j, :4] = quat(mat(dq) @ mat(q_idle))
            else:
                d[f, j, :4] = q_idle
            d[f, j, 4:] = idle_mean[j][1]


def steps(d, names, parents):
    """The frames each foot is planted: where it is farthest forward, the two half a cycle apart."""
    out = []
    for side in ("L", "R"):
        foot = names.index(f"Bip01_{side}_Foot")
        out.append(int(np.argmax([clip_world(d, f, parents)[foot][3, 2] for f in range(d.shape[0])])))
    n, half = d.shape[0], d.shape[0] / 2
    first = (out[0] + (out[1] - half)) / 2
    return sorted(int(round(x)) % n for x in (first, first + half))


def build(game: Game, code: str, armed: bool = True):
    """`code`'s walk; with `armed` False the Keyblade's wrist is left as the walk has it (Roxas's unarmed walk).
    (frames, joints, 7), the weapons and their hands, how each arm is held, the footsteps, the keychain."""
    data, (names, parents, rest, run, idle), (en, src) = event_walk(game, code)
    frames = data.shape[0]
    rest_r = [r[:3, :3] for r in rest]
    rest_t = [r[3, :3] for r in rest]
    runp = [local_pose(run, f, rest_r, rest_t) for f in range(run.frames)]
    idlep = [local_pose(idle, f, rest_r, rest_t) for f in range(idle.frames)]
    idle_mean = mean_pose(idlep)
    held = weapons(names, parents, world(runp[0], parents))
    grip = in_hand(idlep, parents, held)
    hands = {names[h].split("_")[1] for h in held.values()} | set(WEAPON_HAND.get(code, ""))

    # the arms the walk's own
    for f in range(frames):
        for j, n in enumerate(names):
            if arm_side(n) and n in en:
                data[f, j, :4] = quat(src[f][en.index(n)][0])
                data[f, j, 4:] = rest[j][3, :3]

    how = {}
    pose = idle_mean
    if code in MIRROR_ARMS:
        pose, x, hand_flip = mirrored_arms(names, parents, rest, idle_mean, MIRROR_ARMS[code])
        left = next(w for w, h in held.items() if names[h] == "Bip01_L_Hand")
        right = next(w for w, h in held.items() if names[h] == "Bip01_R_Hand")
        grip[right] = mirrored_grip(grip[left], hand_flip)
        how["mirror"] = "the left hand %.2f from the middle, the right one and its weapon mirrored" % x
    for side in sorted(hands):
        if code in IDLE_ARMS:
            idle_arm(data, names, parents, side, pose, IDLE_SWING)
            how[side] = "the idle's arm" + (", mirrored" if code in MIRROR_ARMS and side == "R" else "")
        elif code in KEYBLADE_FORWARD:
            how[side] = "the walk's arm and hand"
            if armed:
                damp_swing(data, names, side, HOLDING_SWING)
                hand = names.index(f"Bip01_{side}_Hand")
                weapon = next(w for w, h in held.items() if h == hand)
                angle, lowest = blade_forward(
                    data, names, parents, hand, weapon, grip, LEFT_FORWARD if side == "L" else FORWARD
                )
                how[side] = (
                    "the walk's arm (%d%% of its swing), the wrist turned %.0f degrees (the tip down to %.2f)"
                    % (
                        round(HOLDING_SWING * 100),
                        angle,
                        lowest,
                    )
                )
        else:
            # the idle's wrist: the hand (and its fingers) as the idle turns them on the forearm
            for j, n in enumerate(names):
                if arm_side(n) == side and ("Hand" in n or "Finger" in n):
                    for f in range(frames):
                        data[f, j, :4] = quat(idle_mean[j][0])
                        data[f, j, 4:] = idle_mean[j][1]
            how[side] = "the walk's arm, the idle's wrist"
    place_weapons(data, names, parents, held, grip)
    for f in range(1, frames):
        for j in range(len(names)):
            if np.dot(data[f, j, :4], data[f - 1, j, :4]) < 0:
                data[f, j, :4] *= -1
    hang = None
    if armed and code in KEYCHAIN:
        folder, node = KEYCHAIN[code]
        weapon = next((w for w, h in held.items() if names[h] == "Bip01_R_Hand"), next(iter(held)))
        quats, most = keychain(data, names, parents, weapon, frames / CYCLE_SECONDS, HEIGHT_M[code])
        hang = (folder, node, quats)
        how["keychain"] = "hangs from the pommel, at most %.1f degrees off straight down" % most
    return data, {names[w]: names[h] for w, h in held.items()}, how, steps(data, names, parents), hang


def weapon_clip(game: Game, code: str, walk: np.ndarray) -> np.ndarray:
    """<code>_w.khw: for each holding hand (WEAPON_HAND), the hand in the player model's space frame by frame of
    the walk (as written: f32), and that hand as the idle (averaged) holds it."""
    names, parents, rest, run, idle = game.field(code)
    rest_r = [r[:3, :3] for r in rest]
    rest_t = [r[3, :3] for r in rest]
    idle_world = world(mean_pose([local_pose(idle, f, rest_r, rest_t) for f in range(idle.frames)]), parents)
    hands = [names.index(f"Bip01_{side}_Hand") for side in WEAPON_HAND[code]]
    d = walk.astype("<f4")
    out = np.zeros((d.shape[0], 2 * len(hands), 7), "<f4")
    for f in range(d.shape[0]):
        w = clip_world(d, f, parents)
        for h, hand in enumerate(hands):
            out[f, 2 * h, :4] = quat(w[hand][:3, :3])
            out[f, 2 * h, 4:] = w[hand][3, :3]
            out[f, 2 * h + 1, :4] = quat(idle_world[hand][:3, :3])
            out[f, 2 * h + 1, 4:] = idle_world[hand][3, :3]
    for f in range(1, d.shape[0]):
        for k in range(0, 2 * len(hands), 2):
            if np.dot(out[f, k, :4], out[f - 1, k, :4]) < 0:
                out[f, k, :4] *= -1
    return out


def write(path: Path, d, fps, plants=None, hang=None) -> None:
    out = bytearray(b"KHW1" + struct.pack("<IIf", d.shape[0], d.shape[1], fps))
    out += np.asarray(d, "<f4").tobytes()
    if plants is not None:
        out += b"STEP" + struct.pack("<I%dI" % len(plants), len(plants), *plants)
    if hang:
        folder, node, quats = hang
        out += b"HANG" + folder.encode().ljust(4, b"\0") + struct.pack("<II", node, len(quats))
        out += np.asarray(quats, "<f4").tobytes()
    path.write_bytes(bytes(out))


def make(nitrofs: Path, mod: Path, codes: list[str]) -> None:
    game = Game(nitrofs)
    out = mod / "animations" / "walk"
    out.mkdir(parents=True, exist_ok=True)
    walks = {}
    for code in codes:
        if code in NO_WALK:
            for stale in (f"{code}.khw", f"{code}_k.khw", f"{code}_w.khw"):
                (out / stale).unlink(missing_ok=True)
            print(code, "no walk: the game's own run, slowed")
            continue
        # Roxas has an unarmed walk too (the story's field): ro.khw with the walk's own hand, ro_k.khw with the
        # Keyblade's wrist, which walk.c plays while his armed motions play; the others one clip
        two = code == "ro"
        data, held, how, plants, hang = build(game, code, armed=not two)
        fps = data.shape[0] / CYCLE_SECONDS
        write(out / f"{code}.khw", data, fps, plants, hang)
        walks[code] = (data, fps)
        if two:
            armed, _, how["armed"], _, armed_hang = build(game, code, armed=True)
            write(out / f"{code}_k.khw", armed, fps, plants, armed_hang)
        else:
            (out / f"{code}_k.khw").unlink(missing_ok=True)
        if code in WEAPON_HAND:
            write(out / f"{code}_w.khw", weapon_clip(game, code, data), fps)
        print(code, "frames", data.shape[0], "fps %.2f" % fps, "steps", plants, "weapons", held, how)
    switch = mod / "days-mods" / "walk.txt"
    switch.parent.mkdir(parents=True, exist_ok=True)
    switch.write_text(mod.name + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("codes", nargs="*", default=CODES, help="the characters (ba/ch/<code>), all by default")
    ap.add_argument("--data", type=Path, help="the port's extracted game data (data/extracted/<hash>)")
    ap.add_argument("--mod", default="Days - Walk")
    args = ap.parse_args()
    data = args.data
    if data is None:
        found = sorted((ROOT / "data" / "extracted").glob("*/nitrofs"))
        if not found:
            ap.error("no extracted game data in data/extracted: give --data")
        data = found[0].parent
    make(data / "nitrofs", ROOT / "mods" / args.mod, args.codes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
