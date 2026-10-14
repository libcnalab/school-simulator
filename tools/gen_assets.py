#!/usr/bin/env python3
"""Generates every texture and sound of school-simulator procedurally (needs numpy and Pillow).

    python3 tools/gen_assets.py [textures|audio|all]

Nothing here comes from an asset pack: textures are noise and geometry, sounds are synthesised.
The generated files are committed, so building the project does not need Python. The output is
deterministic (fixed seeds), so regenerating produces identical files.
"""
import os
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content")
TEX = os.path.join(ROOT, "Textures")
AUD = os.path.join(ROOT, "Audio")
SIZE = 256


# ---------------------------------------------------------------------------------------------- helpers
def rng(seed):
    return np.random.default_rng(seed)


def tile_noise(size, cells, seed):
    """Value noise that tiles: random grid with wrap-around, smooth-interpolated."""
    g = rng(seed).random((cells, cells))
    xs = np.linspace(0, cells, size, endpoint=False)
    x0 = np.floor(xs).astype(int)
    f = xs - x0
    f = f * f * (3 - 2 * f)
    x1 = (x0 + 1) % cells
    a = g[np.ix_(x0, x0)] * (1 - f)[None, :] * (1 - f)[:, None]
    b = g[np.ix_(x0, x1)] * f[None, :] * (1 - f)[:, None]
    c = g[np.ix_(x1, x0)] * (1 - f)[None, :] * f[:, None]
    d = g[np.ix_(x1, x1)] * f[None, :] * f[:, None]
    return a + b + c + d


def fbm(size, seed, octaves=4, base=4):
    out = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        out += amp * tile_noise(size, base * 2 ** o, seed + o * 17)
        total += amp
        amp *= 0.5
    return out / total


def save(name, arr, alpha=None):
    arr = np.clip(arr, 0, 1)
    rgb = (arr * 255 + 0.5).astype(np.uint8)
    if rgb.ndim == 2:
        rgb = np.stack([rgb] * 3, axis=-1)
    if alpha is not None:
        a = (np.clip(alpha, 0, 1) * 255 + 0.5).astype(np.uint8)
        rgb = np.concatenate([rgb, a[..., None]], axis=-1)
        img = Image.fromarray(rgb, "RGBA")
    else:
        img = Image.fromarray(rgb, "RGB")
    os.makedirs(TEX, exist_ok=True)
    img.save(os.path.join(TEX, name + ".png"), optimize=True)
    print("texture", name, img.size)


def tinted(gray, rgb):
    return gray[..., None] * np.array(rgb)[None, None, :]


def grid_lines(size, n, width):
    """1 on grout lines of an n x n tile pattern."""
    idx = np.arange(size)
    cell = size / n
    d = np.minimum(idx % cell, cell - (idx % cell))
    line = (d < width).astype(float)
    return np.maximum(line[None, :], line[:, None])


# ---------------------------------------------------------------------------------------------- textures
def textures():
    n = fbm(SIZE, 1, 5, 8)
    # floors
    t = 0.80 + 0.10 * (n - 0.5)
    g = grid_lines(SIZE, 4, 1.2)
    tile_var = np.kron(rng(2).random((4, 4)), np.ones((SIZE // 4, SIZE // 4))) * 0.06
    save("floor_tile", tinted(t * (1 - 0.30 * g) + tile_var, (0.93, 0.94, 0.95)))

    speck = rng(3).random((SIZE, SIZE))
    terr = 0.74 + 0.08 * (fbm(SIZE, 4, 4, 4) - 0.5) + 0.20 * (speck > 0.985) - 0.18 * (speck < 0.02)
    save("floor_terrazzo", tinted(terr, (0.86, 0.88, 0.84)))

    # wood: planks with grain
    plank = SIZE // 8
    grain = fbm(SIZE, 5, 3, 2)
    wood = np.zeros((SIZE, SIZE))
    for i in range(8):
        r = rng(10 + i)
        wood[i * plank:(i + 1) * plank, :] = 0.62 + 0.18 * r.random()
    gx = np.sin((np.arange(SIZE)[None, :] * 0.10) + grain * 9.0) * 0.5 + 0.5
    wood = wood * (0.82 + 0.18 * gx) - 0.22 * grid_lines_rows(SIZE, 8, 1.0)
    save("floor_wood", np.stack([wood * 0.86, wood * 0.60, wood * 0.36], axis=-1))

    conc = 0.50 + 0.22 * (fbm(SIZE, 6, 5, 6) - 0.5) + 0.05 * (rng(7).random((SIZE, SIZE)) - 0.5)
    save("floor_concrete", tinted(conc, (0.80, 0.80, 0.82)))

    carpet = 0.42 + 0.20 * (rng(8).random((SIZE, SIZE)) - 0.5) + 0.10 * (fbm(SIZE, 9, 3, 8) - 0.5)
    save("floor_carpet", tinted(carpet, (0.45, 0.52, 0.72)))

    grass = 0.45 + 0.30 * (fbm(SIZE, 11, 5, 8) - 0.5) + 0.18 * (rng(12).random((SIZE, SIZE)) - 0.5)
    save("floor_grass", np.stack([grass * 0.55, grass * 1.05, grass * 0.38], axis=-1))

    asp = 0.30 + 0.12 * (fbm(SIZE, 13, 4, 8) - 0.5) + 0.12 * (rng(14).random((SIZE, SIZE)) - 0.5)
    save("floor_asphalt", tinted(asp, (0.95, 0.95, 1.0)))

    # paving: slabs
    pav = 0.62 + 0.10 * (fbm(SIZE, 15, 4, 6) - 0.5)
    pg = grid_lines(SIZE, 4, 1.5)
    pvar = np.kron(rng(16).random((4, 4)), np.ones((SIZE // 4, SIZE // 4))) * 0.10
    save("floor_paving", tinted(pav * (1 - 0.35 * pg) + pvar - 0.05, (0.86, 0.82, 0.76)))

    # running track: red rubber with lane lines across (tiles along x)
    trk = 0.55 + 0.10 * (fbm(SIZE, 17, 4, 8) - 0.5) + 0.06 * (rng(18).random((SIZE, SIZE)) - 0.5)
    img = np.stack([trk * 1.15, trk * 0.42, trk * 0.30], axis=-1)
    for y in (0, SIZE // 2):
        img[max(0, y - 2):y + 2, :, :] = 0.95
    save("floor_track", img)

    sand = 0.78 + 0.12 * (rng(19).random((SIZE, SIZE)) - 0.5) + 0.06 * (fbm(SIZE, 20, 3, 8) - 0.5)
    save("floor_sand", np.stack([sand * 0.96, sand * 0.86, sand * 0.62], axis=-1))

    # gym court (does not tile; one texture across the court area)
    court = np.zeros((SIZE * 2, SIZE * 2, 3))
    base = 0.72 + 0.10 * np.sin(np.arange(SIZE * 2)[None, :] * 0.5 + fbm(SIZE * 2, 21, 3, 2) * 6.0)
    court[..., 0] = base * 0.92
    court[..., 1] = base * 0.66
    court[..., 2] = base * 0.38
    img = Image.fromarray((np.clip(court, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    W2, H2 = SIZE * 2, SIZE * 2
    mx, my = 40, 110
    line = (245, 245, 235)
    d.rectangle([mx, my, W2 - mx, H2 - my], outline=line, width=5)
    d.line([W2 // 2, my, W2 // 2, H2 - my], fill=line, width=5)
    d.ellipse([W2 // 2 - 55, H2 // 2 - 55, W2 // 2 + 55, H2 // 2 + 55], outline=line, width=5)
    for side in (0, 1):
        x0 = mx if side == 0 else W2 - mx
        sgn = 1 if side == 0 else -1
        d.rectangle([min(x0, x0 + sgn * 90), H2 // 2 - 45, max(x0, x0 + sgn * 90), H2 // 2 + 45], outline=line, width=5)
        d.arc([x0 + sgn * 90 - 90, H2 // 2 - 90, x0 + sgn * 90 + 90, H2 // 2 + 90], 270 if side == 0 else 90, 90 if side == 0 else 270, fill=line, width=5)
    os.makedirs(TEX, exist_ok=True)
    img.save(os.path.join(TEX, "gym_court.png"), optimize=True)
    print("texture gym_court", img.size)

    # football pitch markings (does not tile)
    pitch = fbm(SIZE * 2, 22, 5, 8)
    stripes = ((np.arange(SIZE * 2) // 32) % 2)[None, :] * 0.07
    gr = 0.50 + 0.18 * (pitch - 0.5) + stripes
    arr = np.stack([gr * 0.50, gr * 1.0, gr * 0.36], axis=-1)
    img = Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    pm = 14
    d.rectangle([pm, pm, W2 - pm, H2 - pm], outline=(240, 245, 240), width=4)
    d.line([W2 // 2, pm, W2 // 2, H2 - pm], fill=(240, 245, 240), width=4)
    d.ellipse([W2 // 2 - 60, H2 // 2 - 60, W2 // 2 + 60, H2 // 2 + 60], outline=(240, 245, 240), width=4)
    for side in (0, 1):
        x0 = pm if side == 0 else W2 - pm
        sgn = 1 if side == 0 else -1
        d.rectangle([min(x0, x0 + sgn * 70), H2 // 2 - 110, max(x0, x0 + sgn * 70), H2 // 2 + 110], outline=(240, 245, 240), width=4)
    img.save(os.path.join(TEX, "pitch.png"), optimize=True)
    print("texture pitch", img.size)

    # walls
    plaster = 0.90 + 0.06 * (fbm(SIZE, 23, 5, 8) - 0.5) + 0.03 * (rng(24).random((SIZE, SIZE)) - 0.5)
    save("wall_plaster", tinted(plaster, (1.0, 1.0, 1.0)))

    # brick: running bond
    brick = np.zeros((SIZE, SIZE, 3))
    bh, bw = SIZE // 16, SIZE // 8
    r = rng(25)
    for row in range(16):
        off = (bw // 2) if row % 2 else 0
        for col in range(-1, 9):
            x0 = col * bw + off
            col_v = 0.52 + 0.20 * r.random()
            ys = slice(row * bh + 2, (row + 1) * bh - 1)
            for x in range(x0 + 2, x0 + bw - 1):
                brick[ys, x % SIZE, :] = (col_v * 0.82, col_v * 0.38, col_v * 0.28)
    mortar = np.array([0.72, 0.70, 0.66])
    mask = brick.sum(axis=-1) == 0
    brick[mask] = mortar
    brick *= (0.9 + 0.2 * fbm(SIZE, 26, 4, 8))[..., None]
    save("wall_brick", brick)

    wc = 0.62 + 0.14 * (fbm(SIZE, 27, 5, 6) - 0.5)
    save("wall_concrete", tinted(wc, (0.90, 0.90, 0.92)))

    # ceramic tiles for toilets
    ct = 0.88 + 0.04 * (rng(28).random((SIZE, SIZE)) - 0.5)
    cg = grid_lines(SIZE, 8, 1.0)
    save("wall_tile", tinted(ct * (1 - 0.35 * cg), (0.88, 0.95, 0.96)))

    # lower wall panelling
    panel = np.zeros((SIZE, SIZE))
    for i in range(8):
        panel[:, i * (SIZE // 8):(i + 1) * (SIZE // 8)] = 0.55 + 0.10 * rng(30 + i).random()
    gr2 = np.sin(np.arange(SIZE)[:, None] * 0.07 + fbm(SIZE, 29, 3, 2) * 8.0) * 0.5 + 0.5
    panel = panel * (0.82 + 0.18 * gr2) - 0.25 * grid_lines_cols(SIZE, 8, 1.0)
    save("wall_panel", np.stack([panel * 0.84, panel * 0.60, panel * 0.38], axis=-1))

    # ceiling tile with grid and tiny perforation
    cl = 0.92 + 0.04 * (rng(31).random((SIZE, SIZE)) - 0.5)
    pf = (rng(32).random((SIZE, SIZE)) > 0.97) * 0.08
    cgx = grid_lines(SIZE, 2, 1.5)
    save("ceiling_tile", tinted(cl - pf - 0.18 * cgx, (0.97, 0.97, 0.98)))

    # roof
    roof = 0.34 + 0.10 * (fbm(SIZE, 33, 4, 8) - 0.5) + 0.12 * grid_lines_rows(SIZE, 16, 1.0) * -1
    save("roof", tinted(roof, (0.62, 0.55, 0.52)))

    # doors
    door = np.zeros((SIZE, SIZE))
    door[:] = 0.62
    door = door * (0.85 + 0.15 * (np.sin(np.arange(SIZE)[:, None] * 0.12 + fbm(SIZE, 34, 3, 2) * 7) * 0.5 + 0.5))
    img = np.stack([door * 0.86, door * 0.58, door * 0.34], axis=-1)
    img[SIZE // 8:SIZE // 2 - 8, SIZE // 5:SIZE - SIZE // 5, :] *= 0.8   # raised panels read as darker
    img[SIZE // 2 + 8:SIZE - SIZE // 8, SIZE // 5:SIZE - SIZE // 5, :] *= 0.8
    save("door_wood", img)

    metal = 0.55 + 0.10 * (fbm(SIZE, 35, 4, 8) - 0.5)
    img = tinted(metal, (0.60, 0.64, 0.68))
    img[::64, :, :] *= 0.8
    save("door_metal", img)

    # glass: translucent blue with a sky glint
    yy, xx = np.mgrid[0:SIZE, 0:SIZE] / SIZE
    glint = np.clip(1.0 - np.abs((xx + yy) - 0.9) * 8.0, 0, 1)
    glass = np.stack([0.55 + 0.25 * glint, 0.72 + 0.2 * glint, 0.85 + 0.1 * glint], axis=-1)
    save("glass", glass, alpha=0.30 + 0.25 * glint)

    # whiteboard with a frame
    wb = np.ones((SIZE, SIZE, 3)) * 0.96
    wb[:6, :, :] = wb[-6:, :, :] = 0.55
    wb[:, :6, :] = wb[:, -6:, :] = 0.55
    wb += 0.02 * (rng(36).random((SIZE, SIZE, 1)) - 0.5)
    img = Image.fromarray((np.clip(wb, 0, 1) * 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    for i in range(7):
        y = 38 + i * 28
        d.line([24, y, 24 + int(rng(40 + i).random() * 160) + 40, y], fill=(40, 80, 160), width=3)
    d.ellipse([170, 150, 220, 200], outline=(200, 40, 40), width=3)
    img.save(os.path.join(TEX, "whiteboard.png"), optimize=True)
    print("texture whiteboard", img.size)

    # book spines: tiles horizontally; rows of books of different colours and heights
    books = Image.new("RGB", (SIZE, SIZE), (60, 40, 30))
    d = ImageDraw.Draw(books)
    r = rng(50)
    for row in range(4):
        y0, y1 = row * 64 + 4, row * 64 + 60
        x = 0
        while x < SIZE:
            w = int(5 + r.random() * 10)
            top = y0 + int(r.random() * 10)
            col = tuple(int(c) for c in (60 + r.random(3) * 180))
            d.rectangle([x, top, min(SIZE, x + w) - 1, y1], fill=col)
            d.line([x, top + 6, min(SIZE, x + w) - 1, top + 6], fill=(230, 220, 190), width=1)
            x += w
        d.rectangle([0, y1, SIZE, y1 + 3], fill=(95, 65, 40))
    books.save(os.path.join(TEX, "books.png"), optimize=True)
    print("texture books", books.size)

    # screen glow (computer / signage base): dark blue with scanlines
    sc = np.zeros((SIZE, SIZE, 3))
    sc[..., 0] = 0.06
    sc[..., 1] = 0.12
    sc[..., 2] = 0.30
    sc *= (0.85 + 0.15 * (np.arange(SIZE)[:, None] % 4 < 2))
    save("screen", sc)

    # posters: abstract, bright
    for i in range(4):
        r = rng(60 + i)
        img = Image.new("RGB", (128, 192), tuple(int(c) for c in (r.random(3) * 120 + 120)))
        d = ImageDraw.Draw(img)
        for _ in range(7):
            x0, y0 = r.integers(0, 90), r.integers(0, 150)
            x1, y1 = x0 + r.integers(20, 60), y0 + r.integers(20, 60)
            col = tuple(int(c) for c in (r.random(3) * 255))
            if r.random() < 0.5:
                d.ellipse([x0, y0, x1, y1], fill=col)
            else:
                d.rectangle([x0, y0, x1, y1], fill=col)
        d.rectangle([0, 0, 127, 191], outline=(30, 30, 30), width=4)
        img.save(os.path.join(TEX, "poster_%d.png" % (i + 1)), optimize=True)
        print("texture poster", i + 1)

    # a plain white pixel for flat-coloured geometry
    save("white", np.ones((8, 8, 3)))

    # sky gradient strip not needed (the dome is vertex coloured), but a cloud layer is nice
    cloud = np.clip((fbm(SIZE, 70, 5, 4) - 0.45) * 3.0, 0, 1)
    save("clouds", np.ones((SIZE, SIZE, 3)), alpha=cloud * 0.9)

    # leaves for trees
    leaf = 0.5 + 0.35 * (fbm(SIZE, 71, 5, 8) - 0.5)
    save("leaves", np.stack([leaf * 0.35, leaf * 0.9, leaf * 0.25], axis=-1))
    bark = 0.40 + 0.25 * (fbm(SIZE, 72, 4, 3) - 0.5)
    save("bark", np.stack([bark * 0.62, bark * 0.46, bark * 0.32], axis=-1))
    # soft round particle for rain/snow/fog
    yy, xx = np.mgrid[0:64, 0:64]
    dd = np.sqrt((xx - 31.5) ** 2 + (yy - 31.5) ** 2) / 32.0
    soft = np.clip(1 - dd, 0, 1) ** 1.5
    save("particle", np.ones((64, 64, 3)), alpha=soft)


def grid_lines_rows(size, n, width):
    idx = np.arange(size)
    cell = size / n
    d = np.minimum(idx % cell, cell - (idx % cell))
    return (d < width).astype(float)[:, None] * np.ones((1, size))


def grid_lines_cols(size, n, width):
    return grid_lines_rows(size, n, width).T


# ---------------------------------------------------------------------------------------------- audio
SR = 22050


def write_wav(name, samples):
    os.makedirs(AUD, exist_ok=True)
    x = np.clip(samples, -1, 1)
    data = (x * 32767).astype("<i2")
    with wave.open(os.path.join(AUD, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())
    print("audio", name, "%.1fs" % (len(x) / SR))


def tvec(seconds):
    return np.arange(int(SR * seconds)) / SR


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc = a * acc + (1 - a) * v
        y[i] = acc
    return y


def smooth_noise(seconds, cutoff, seed):
    n = rng(seed).standard_normal(int(SR * seconds))
    # cheap FFT-domain low-pass
    f = np.fft.rfft(n)
    freqs = np.fft.rfftfreq(len(n), 1 / SR)
    f *= 1.0 / (1.0 + (freqs / cutoff) ** 4)
    out = np.fft.irfft(f, len(n))
    return out / (np.max(np.abs(out)) + 1e-9)


def band_noise(seconds, lo, hi, seed):
    n = rng(seed).standard_normal(int(SR * seconds))
    f = np.fft.rfft(n)
    freqs = np.fft.rfftfreq(len(n), 1 / SR)
    f *= ((freqs > lo) & (freqs < hi)).astype(float)
    out = np.fft.irfft(f, len(n))
    return out / (np.max(np.abs(out)) + 1e-9)


def loop_fade(x, fade=0.25):
    """Makes a loop seamless by cross-fading the end into the start."""
    n = int(SR * fade)
    out = x[:-n].copy()
    w = np.linspace(0, 1, n)
    out[:n] = out[:n] * w + x[-n:] * (1 - w)
    return out


def formant_voice(seconds, seed, f0=120, vowels=None):
    """A babble that sounds like speech from far away: glottal pulses through moving formants."""
    r = rng(seed)
    t = tvec(seconds)
    n = len(t)
    vowel_set = vowels or [(730, 1090), (270, 2290), (300, 870), (530, 1840), (570, 840)]
    out = np.zeros(n)
    pos = 0
    pitch = f0
    while pos < n:
        length = int(SR * (0.08 + 0.12 * r.random()))
        f1, f2 = vowel_set[r.integers(0, len(vowel_set))]
        seg = np.arange(length) / SR
        pitch = f0 * (0.85 + 0.35 * r.random())
        glottal = np.sign(np.sin(2 * np.pi * pitch * seg)) * 0.3 + np.sin(2 * np.pi * pitch * seg) * 0.4
        env = np.sin(np.pi * seg / seg[-1]) ** 0.7
        vowel = (np.sin(2 * np.pi * f1 * seg) * 0.5 + np.sin(2 * np.pi * f2 * seg) * 0.3) * glottal
        end = min(n, pos + length)
        out[pos:end] += (vowel * env)[:end - pos]
        pos += int(length * (0.8 if r.random() < 0.8 else 1.7))
    out += 0.03 * smooth_noise(seconds, 4000, seed + 1)
    return out / (np.max(np.abs(out)) + 1e-9)


def audio():
    r = rng(100)
    # ---- school bell: a struck metal bell rung rapidly (clapper at ~11 Hz), several inharmonic partials
    t = tvec(4.0)
    bell = np.zeros_like(t)
    hits = np.arange(0, 3.6, 1 / 11.0)
    for h in hits:
        tt = t - h
        m = tt > 0
        env = np.where(m, np.exp(-tt * 9.0), 0.0)
        tone = (np.sin(2 * np.pi * 1180 * tt) + 0.7 * np.sin(2 * np.pi * 1880 * tt) + 0.5 * np.sin(2 * np.pi * 2760 * tt) + 0.3 * np.sin(2 * np.pi * 3510 * tt))
        bell += tone * env
    fade = np.minimum(1, (4.0 - t) * 3)
    write_wav("bell", 0.5 * bell / np.max(np.abs(bell)) * fade)

    # ---- fire alarm: alternating two tones, hard edged
    t = tvec(4.0)
    sw = ((t * 2) % 2 < 1)
    alarm = np.where(sw, np.sign(np.sin(2 * np.pi * 960 * t)), np.sign(np.sin(2 * np.pi * 770 * t))) * 0.25 + 0.15 * np.sin(2 * np.pi * np.where(sw, 960, 770) * t)
    write_wav("alarm", loop_fade(np.concatenate([alarm, alarm[:int(SR * 0.3)]]), 0.3) * 0.7)

    # ---- PA chime: ding-dong
    t = tvec(2.2)
    chime = np.zeros_like(t)
    for start, freq in ((0.0, 659.3), (0.55, 523.3)):
        tt = t - start
        env = np.where(tt > 0, np.exp(-tt * 2.6), 0)
        chime += env * (np.sin(2 * np.pi * freq * tt) + 0.4 * np.sin(2 * np.pi * freq * 2 * tt) + 0.2 * np.sin(2 * np.pi * freq * 3.01 * tt))
    write_wav("chime", 0.6 * chime / np.max(np.abs(chime)))

    # ---- announcements: two babbles of different lengths
    write_wav("announce1", formant_voice(3.2, 110, 118) * 0.55)
    write_wav("announce2", formant_voice(4.6, 111, 205) * 0.55)

    # ---- footsteps and doors
    t = tvec(0.14)
    step = band_noise(0.14, 120, 1600, 120) * np.exp(-t * 38) * 0.9
    write_wav("step", step * 0.8)
    t = tvec(0.14)
    step2 = band_noise(0.14, 200, 3000, 121) * np.exp(-t * 55)
    write_wav("step_hard", step2 * 0.8)
    t = tvec(0.5)
    click = band_noise(0.5, 80, 900, 122) * np.exp(-t * 14) * 0.7 + np.sin(2 * np.pi * 180 * t) * np.exp(-t * 30) * 0.5
    write_wav("door_close", click)
    t = tvec(0.35)
    op = band_noise(0.35, 300, 2500, 123) * (np.exp(-t * 30) * 0.7 + np.exp(-(t - 0.2) * 20) * (t > 0.2) * 0.4)
    write_wav("door_open", op * 0.8)
    t = tvec(1.0)
    slide = band_noise(1.0, 200, 1800, 124) * np.sin(np.pi * t) * 0.35
    write_wav("door_slide", slide)

    # ---- ambient loops
    def crowd(seconds, seed, voices, brightness):
        out = np.zeros(int(SR * seconds))
        for v in range(voices):
            out += formant_voice(seconds, seed + v, 100 + 12 * v) * (0.5 + 0.5 * rng(seed + v).random()) / voices ** 0.5
        out += band_noise(seconds, 300, brightness, seed + 50) * 0.35
        return out / (np.max(np.abs(out)) + 1e-9)

    write_wav("chatter", loop_fade(crowd(7.0, 200, 8, 3500)) * 0.7)
    cafe = crowd(7.0, 230, 14, 4500)
    t = tvec(7.0)
    for h in r.random(30) * 6.5:
        tt = t - h
        cafe += np.where(tt > 0, np.sin(2 * np.pi * (2500 + 800 * r.random()) * tt) * np.exp(-tt * 40), 0) * 0.25   # cutlery
    write_wav("cafeteria", loop_fade(cafe / np.max(np.abs(cafe))) * 0.7)
    write_wav("hall", loop_fade(smooth_noise(7.0, 400, 240) * 0.5 + band_noise(7.0, 600, 2200, 241) * 0.12) * 0.5)
    write_wav("classroom", loop_fade(smooth_noise(7.0, 500, 250) * 0.18 + crowd(7.0, 251, 3, 1800) * 0.16) * 0.5)
    write_wav("ambience", loop_fade(smooth_noise(7.0, 250, 260) * 0.35) * 0.5)

    # outdoor: wind plus bird chirps
    t = tvec(8.0)
    out = smooth_noise(8.0, 800, 270) * 0.35 * (0.6 + 0.4 * np.sin(2 * np.pi * 0.2 * t))
    for h in r.random(14) * 7.0:
        tt = t - h
        f = 3200 + 900 * np.sin(2 * np.pi * 8 * tt) + 600 * r.random()
        out += np.where((tt > 0) & (tt < 0.25), np.sin(2 * np.pi * f * tt) * np.sin(np.pi * tt / 0.25), 0) * 0.12
    write_wav("outdoor", loop_fade(out) * 0.7)

    # gym: reverberant bounces and squeaks
    t = tvec(7.0)
    g = smooth_noise(7.0, 300, 280) * 0.12
    for h in np.arange(0.3, 6.6, 0.55 + 0.1 * r.random()):
        tt = t - h
        g += np.where(tt > 0, np.sin(2 * np.pi * 95 * tt) * np.exp(-tt * 22), 0) * 0.5
    for h in r.random(5) * 6.0:
        tt = t - h
        g += np.where((tt > 0) & (tt < 0.15), np.sin(2 * np.pi * (1800 + 3000 * tt) * tt) * np.sin(np.pi * tt / 0.15), 0) * 0.1
    echo = np.zeros_like(g)
    for delay, amp in ((0.12, 0.5), (0.27, 0.3), (0.45, 0.2)):
        d = int(SR * delay)
        echo[d:] += g[:-d] * amp
    write_wav("gym", loop_fade((g + echo) / np.max(np.abs(g + echo))) * 0.7)
    t = tvec(0.5)
    write_wav("bounce", np.sin(2 * np.pi * 105 * t) * np.exp(-t * 14) * 0.9)

    # computer lab: fans and clicking keys
    t = tvec(7.0)
    c = smooth_noise(7.0, 700, 290) * 0.28 + np.sin(2 * np.pi * 120 * t) * 0.06
    for h in r.random(80) * 6.8:
        tt = t - h
        c += np.where(tt > 0, band_noise(7.0, 1500, 6000, 291 + int(h * 10))[:len(t)] * np.exp(-tt * 120), 0) * 0.18
    write_wav("computer", loop_fade(c / np.max(np.abs(c))) * 0.5)

    # workshop machinery
    t = tvec(6.0)
    m = np.sign(np.sin(2 * np.pi * 70 * t)) * 0.2 + smooth_noise(6.0, 1200, 295) * 0.4 + np.sin(2 * np.pi * 410 * t) * 0.1 * (np.sin(2 * np.pi * 0.7 * t) > 0)
    write_wav("machine", loop_fade(m / np.max(np.abs(m))) * 0.5)

    # music room: plucked-string melody (Karplus-Strong)
    def pluck(freq, seconds):
        n = int(SR * seconds)
        buf = rng(int(freq)).uniform(-1, 1, int(SR / freq))
        out = np.zeros(n)
        idx = 0
        for i in range(n):
            out[i] = buf[idx]
            nxt = (idx + 1) % len(buf)
            buf[idx] = 0.4985 * (buf[idx] + buf[nxt])
            idx = nxt
        return out

    notes = [262, 330, 392, 330, 294, 349, 440, 349, 262, 392, 330, 262]
    song = np.zeros(int(SR * 8.0))
    for i, f in enumerate(notes):
        s = pluck(f, 1.0)
        a = int(SR * 0.62 * i)
        song[a:a + len(s)] += s[:len(song) - a]
    write_wav("music", loop_fade(song / np.max(np.abs(song))) * 0.6)

    # lab: bubbling and a low hum
    t = tvec(6.0)
    b = smooth_noise(6.0, 200, 296) * 0.1
    for h in r.random(40) * 5.8:
        tt = t - h
        b += np.where((tt > 0) & (tt < 0.12), np.sin(2 * np.pi * (300 + 1500 * tt / 0.12) * tt) * np.sin(np.pi * tt / 0.12), 0) * 0.35
    write_wav("lab", loop_fade(b / np.max(np.abs(b))) * 0.5)

    # rain: dense noise
    write_wav("rain", loop_fade(band_noise(6.0, 800, 8000, 297) * 0.7 + smooth_noise(6.0, 500, 298) * 0.2) * 0.6)

    # ui click and cheers
    t = tvec(0.06)
    write_wav("click", np.sin(2 * np.pi * 1400 * t) * np.exp(-t * 60) * 0.7)
    write_wav("cheer", crowd(2.0, 300, 10, 4000) * np.minimum(1, tvec(2.0) * 6) * np.minimum(1, (2.0 - tvec(2.0)) * 4) * 0.8)


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("textures", "all"):
        textures()
    if what in ("audio", "all"):
        audio()
