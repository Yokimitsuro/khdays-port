"""NSBMD textures (TEX0) and materials: each material's texture decoded to RGBA, its texture matrix."""

import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nitro3d import parse_dict  # noqa: E402


def bgr555(v):
    v = np.asarray(v, np.uint32)
    r = (v & 31) * 255 // 31
    g = ((v >> 5) & 31) * 255 // 31
    b = ((v >> 10) & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.uint8)


def parse_tex0(b, o):
    """{name: (param, extra)}, {pal name: offset}, and the data areas."""
    assert b[o : o + 4] == b"TEX0"
    tex_dict = o + struct.unpack_from("<H", b, o + 0xE)[0]
    tex_data = o + struct.unpack_from("<I", b, o + 0x14)[0]
    c_data = o + struct.unpack_from("<I", b, o + 0x24)[0]
    c_idx = o + struct.unpack_from("<I", b, o + 0x28)[0]
    pal_dict = o + struct.unpack_from("<I", b, o + 0x34)[0]
    pal_data = o + struct.unpack_from("<I", b, o + 0x38)[0]
    texs = {}
    num = b[tex_dict + 1]
    # the dictionary's entries are 8 bytes here: parse by hand
    data = tex_dict + 8 + 4 * (num + 1)
    esize = struct.unpack_from("<H", b, data)[0]
    names_o = data + 4 + num * esize
    for i in range(num):
        param, extra = struct.unpack_from("<II", b, data + 4 + i * esize)
        name = b[names_o + 16 * i : names_o + 16 * i + 16].split(b"\0")[0].decode("latin1")
        texs[name] = (param, extra)
    pals = {}
    num = b[pal_dict + 1]
    data = pal_dict + 8 + 4 * (num + 1)
    esize = struct.unpack_from("<H", b, data)[0]
    names_o = data + 4 + num * esize
    for i in range(num):
        off, flag = struct.unpack_from("<HH", b, data + 4 + i * esize)
        name = b[names_o + 16 * i : names_o + 16 * i + 16].split(b"\0")[0].decode("latin1")
        pals[name] = off << 3
    return dict(texs=texs, pals=pals, tex_data=tex_data, c_data=c_data, c_idx=c_idx, pal_data=pal_data)


def decode(b, t, param, pal_off, colour0=None):
    """RGBA (h, w, 4) uint8."""
    off = (param & 0xFFFF) << 3
    w = 8 << ((param >> 20) & 7)
    h = 8 << ((param >> 23) & 7)
    fmt = (param >> 26) & 7
    c0 = (param >> 29) & 1 if colour0 is None else colour0
    n = w * h
    pal = None
    if fmt in (1, 2, 3, 4, 6) and pal_off is not None:
        pa = t["pal_data"] + pal_off
        cnt = {1: 32, 2: 4, 3: 16, 4: 256, 6: 8}[fmt]
        have = max(0, min(cnt, (len(b) - pa) // 2))
        raw = np.zeros(cnt, np.uint16)
        raw[:have] = np.frombuffer(b, np.uint16, have, pa)
        pal = bgr555(raw)
    out = np.zeros((n, 4), np.uint8)
    if fmt == 0:
        return out.reshape(h, w, 4)
    if fmt == 7:
        raw = np.frombuffer(b, np.uint16, n, t["tex_data"] + off)
        out[:, :3] = bgr555(raw)
        out[:, 3] = np.where(raw & 0x8000, 255, 0)
        return out.reshape(h, w, 4)
    if fmt == 5:
        blocks = (w // 4) * (h // 4)
        data = np.frombuffer(b, np.uint32, blocks, t["c_data"] + off)
        idx = np.frombuffer(b, np.uint16, blocks, t["c_idx"] + off // 2)
        img = np.zeros((h, w, 4), np.uint8)
        for k in range(blocks):
            by, bx = divmod(k, w // 4)
            pi = int(idx[k])
            pa = t["pal_data"] + pal_off + (pi & 0x3FFF) * 4
            mode = pi >> 14
            cols = np.frombuffer(b, np.uint16, 4, pa) if pa + 8 <= len(b) else np.zeros(4, np.uint16)
            c = bgr555(cols).astype(int)
            col = np.zeros((4, 4), int)
            col[:, 3] = 255
            col[0, :3] = c[0]
            col[1, :3] = c[1]
            if mode == 0:
                col[2, :3] = c[2]
                col[3, 3] = 0
            elif mode == 1:
                col[2, :3] = (c[0] + c[1]) // 2
                col[3, 3] = 0
            elif mode == 2:
                col[2, :3] = c[2]
                col[3, :3] = c[3]
            else:
                col[2, :3] = (c[0] * 5 + c[1] * 3) // 8
                col[3, :3] = (c[0] * 3 + c[1] * 5) // 8
            d = int(data[k])
            for yy in range(4):
                for xx in range(4):
                    img[by * 4 + yy, bx * 4 + xx] = col[(d >> (2 * (yy * 4 + xx))) & 3]
        return img
    bits = {1: 8, 2: 2, 3: 4, 4: 8, 6: 8}[fmt]
    raw = np.frombuffer(b, np.uint8, n * bits // 8, t["tex_data"] + off)
    if bits == 8:
        v = raw.astype(int)
    else:
        per = 8 // bits
        v = np.zeros(n, int)
        for k in range(per):
            v[k::per] = (raw >> (k * bits)) & ((1 << bits) - 1)
    if fmt == 1:
        i, a = v & 31, v >> 5
        alpha = (a * 4 + a // 2) * 255 // 31
    elif fmt == 6:
        i, a = v & 7, v >> 3
        alpha = a * 255 // 31
    else:
        i = v
        alpha = np.where((v == 0) & (c0 == 1), 0, 255)
    if pal is None:
        pal = np.full((256, 3), 200, np.uint8)
    out[:, :3] = pal[np.clip(i, 0, len(pal) - 1)]
    out[:, 3] = alpha
    return out.reshape(h, w, 4)


def materials(b, m, tex0_off):
    """Per material index: dict(img RGBA or None, tex_param, scale (s, t), alpha (0-31), size)."""
    mo = m + struct.unpack_from("<I", b, m + 8)[0]
    d_tex = mo + struct.unpack_from("<H", b, mo)[0]
    d_pal = mo + struct.unpack_from("<H", b, mo + 2)[0]
    mats = parse_dict(b, mo + 4)
    out = [dict(name=n, img=None) for n, _ in mats]
    for idx, (name, off) in enumerate(mats):
        p = mo + off
        tag, size, diffamb, specemi, polyattr, pmask, teximg, timask, pltt_base, flag, ow, oh = struct.unpack_from(
            "<HHIIIIIIHHHH", b, p
        )
        magw, magh = struct.unpack_from("<ii", b, p + 0x24)
        q = p + 0x2C
        ss, st = 1.0, 1.0
        if flag & 1:
            if not flag & 2:
                ss, st = (v / 4096 for v in struct.unpack_from("<ii", b, q))
                q += 8
        out[idx].update(
            teximg=teximg,
            alpha=(polyattr >> 16) & 31,
            mode=(polyattr >> 4) & 3,
            diffuse=(diffamb & 0x7FFF) if diffamb & 0x8000 else 0x7FFF,
            polyattr=polyattr,
            ow=ow,
            oh=oh,
            scale=(ss, st),
            flag=flag,
        )
    t = parse_tex0(b, tex0_off) if tex0_off is not None else None
    # texture -> materials, palette -> materials
    tex_of, pal_of = {}, {}
    for dct, dst in ((d_tex, tex_of), (d_pal, pal_of)):
        for name, val in parse_dict(b, dct):
            lst = mo + (val & 0xFFFF)
            cnt = (val >> 16) & 0xFF
            for k in range(cnt):
                dst[b[lst + k]] = name
    if t is None:
        return out
    cache = {}
    for idx, mt in enumerate(out):
        tn = tex_of.get(idx)
        if tn is None or tn not in t["texs"]:
            continue
        pn = pal_of.get(idx)
        key = (tn, pn)
        if key not in cache:
            param = t["texs"][tn][0]
            try:
                cache[key] = decode(b, t, param, t["pals"].get(pn))
            except Exception:
                cache[key] = None
        mt["img"] = cache[key]
        mt["tex"] = tn
        mt["pal"] = pn
    return out
