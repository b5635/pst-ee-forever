# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds the README pictures in docs/media from in-game screenshots (BMPs the tests write).

    python -I tools/readme_media.py [--logs DIR] [--fight DIR] [--config DIR]

--logs   the game's pst_forever\\logs (lighting, dice, lock, gore, hotbar shots of the in-game suites)
--fight  shot_NNN.bmp of a fight run with PSTF_SHOT_DIR (turn-based GIF and stills)
--config cfg2.bmp from `pst_forever_config.exe --shot cfg2.bmp --section 2`
A missing source is skipped with a note.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "media")
GAME_LOGS = r"C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs"


def font(size):
    for name in ("georgiab.ttf", "georgia.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def load(path, crop=None, width=None):
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(crop)
    if width and im.width != width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    return im


def label(im, text, size=22):
    d = ImageDraw.Draw(im)
    f = font(size)
    x, y = 12, 10
    box = d.textbbox((x, y), text, font=f)
    d.rectangle((box[0] - 6, box[1] - 4, box[2] + 6, box[3] + 4), fill=(0, 0, 0))
    d.text((x, y), text, font=f, fill=(232, 196, 120))
    return im


def side_by_side(left, right, names, gap=4):
    out = Image.new("RGB", (left.width + right.width + gap, max(left.height, right.height)), (0, 0, 0))
    out.paste(label(left.copy(), names[0]), (0, 0))
    out.paste(label(right.copy(), names[1]), (left.width + gap, 0))
    return out


def save_png(im, name):
    path = os.path.join(OUT, name)
    im.save(path, optimize=True)
    print(f"{name}: {im.width}x{im.height}, {os.path.getsize(path) // 1024} KB")


def save_gif(frames, name, ms, colors=200):
    path = os.path.join(OUT, name)
    pal = [f.quantize(colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    durations = ms if isinstance(ms, list) else [ms] * len(frames)
    pal[0].save(path, save_all=True, append_images=pal[1:], duration=durations, loop=0, optimize=True,
                disposal=1)
    print(f"{name}: {len(frames)} frames {frames[0].width}x{frames[0].height}, {os.path.getsize(path) // 1024} KB")


def have(*paths):
    missing = [p for p in paths if not os.path.exists(p)]
    for p in missing:
        print(f"skip: {p} missing")
    return not missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logs", default=GAME_LOGS)
    ap.add_argument("--fight")
    ap.add_argument("--config")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    L = lambda n: os.path.join(a.logs, n)

    # Turn-based fight: GIF of the run + two stills (movement preview, attack of opportunity).
    if a.fight:
        shots = sorted(f for f in os.listdir(a.fight) if f.startswith("shot_") and f.endswith(".bmp"))
        frames = [load(os.path.join(a.fight, f), width=600) for f in shots[3:61]]
        if frames:
            save_gif(frames, "tbc_fight.gif", 450, colors=96)
        for src, dst in (("shot_011.bmp", "tbc_path.png"), ("shot_027.bmp", "tbc_aoo.png")):
            if have(os.path.join(a.fight, src)):
                save_png(load(os.path.join(a.fight, src), width=960), dst)

    # Lighting: vanilla vs ours.
    for off, lit, dst, crop in (("night_off.bmp", "night_lit.bmp", "lighting_night.png", (200, 60, 1080, 600)),):
        if have(L(off), L(lit)):
            save_png(side_by_side(load(L(off), crop, 640), load(L(lit), crop, 640), ("Vanilla", "PST Forever")), dst)

    hammer = [f"hammer_night_{{}}_{t}.bmp" for t in range(20, 81, 6)]
    if have(*[L(h.format(m)) for h in hammer for m in ("lit", "off")]):
        crop = (380, 140, 1000, 560)
        frames = [side_by_side(load(L(h.format("off")), crop, 450), load(L(h.format("lit")), crop, 450),
                               ("Vanilla", "PST Forever")) for h in hammer]
        save_gif(frames, "lighting_spell.gif", [160] * (len(frames) - 1) + [900])

    # Dialogue ability roll: the hidden reply, the panel, the throw, the result.
    seq = [("dlg_list_1.bmp", 1800), ("dlg_panel_1.bmp", 1200), ("dlg_tumble.bmp", 500),
           ("dlg_natural_ok.bmp", 900), ("dlg_bonus_ok.bmp", 900), ("dlg_pop_ok.bmp", 700),
           ("dlg_banner_ok_ok.bmp", 1800), ("dlg_won.bmp", 2200)]
    if have(*[L(n) for n, _ in seq]):
        crop = (320, 0, 960, 720)
        save_gif([load(L(n), crop, 480) for n, _ in seq], "dialogue_roll.gif", [ms for _, ms in seq])
        save_png(load(L("dlg_banner_ok_ok.bmp"), (420, 60, 860, 680)), "dialogue_success.png")

    die = ["die3d_idle.bmp", "die3d_0300.bmp", "die3d_0700.bmp", "die3d_1100.bmp", "die3d_1500.bmp",
           "die3d_2100.bmp", "die3d_4250.bmp", "die3d_4600.bmp", "die3d_6400.bmp"]
    if have(*[L(n) for n in die]):
        crop = (440, 40, 840, 520)
        save_gif([load(L(n), crop, 360) for n in die], "die3d.gif", [700, 250, 250, 250, 400, 600, 600, 900, 1500])

    if have(L("lock_bash_ok.bmp")):
        save_png(load(L("lock_bash_ok.bmp"), (420, 60, 860, 680)), "lock_roll.png")
    if have(L("gore_fight.bmp")):
        save_png(load(L("gore_fight.bmp"), (360, 80, 980, 480)), "gore.png")
    if have(L("bar2_desc.bmp")):
        save_png(load(L("bar2_desc.bmp"), (230, 0, 1030, 720), 640), "maneuver_run.png")
    if a.config and have(os.path.join(a.config, "cfg2.bmp")):
        save_png(load(os.path.join(a.config, "cfg2.bmp")), "options_window.png")
    return 0


if __name__ == "__main__":
    sys.exit(main())
