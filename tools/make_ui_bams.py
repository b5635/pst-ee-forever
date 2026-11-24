# SPDX-License-Identifier: GPL-2.0-or-later
"""BAMs for the turn-based hotbar (deployed into the game's override folder by tools/deploy.ps1).

    python -I tools/make_ui_bams.py [--gemrb D:\\gemrb]

  PFENDTRN.BAM  End Turn button from graphics/end_turn.jpg (72x72; frames 0 normal, 1 pressed/lit,
                2 = 1, 3 disabled)
  PFTRIP.BAM, PFWITHDR.BAM, PFSPRINT.BAM  the GemRB fork's turn-based move icons (tbtrip, tbwithdr,
                tbrun: 32x32, same frame layout), copied as they are
  PFPAGEUP.BAM, PFPAGEDN.BAM  hotbar page buttons: gold arrow on a dark plate (22x36, same frames)
"""
import argparse
import shutil
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "override"


def write_bam_v1(path, frames):
    """Uncompressed BAM V1, one cycle of all frames; frames are RGBA arrays of one size.
    Palette index 0 is the transparent green."""
    h, w = frames[0].shape[:2]
    strip = np.concatenate(frames, axis=1)
    rgb = Image.fromarray(strip[..., :3]).quantize(255, method=Image.Quantize.MEDIANCUT)
    idx = np.array(rgb, np.uint8) + 1
    idx[strip[..., 3] < 128] = 0
    pal = rgb.getpalette()[:255 * 3]
    pal += [0] * (255 * 3 - len(pal))
    n = len(frames)
    frames_off = 24
    pal_off = frames_off + 12 * n + 4
    lut_off = pal_off + 1024
    data_off = lut_off + 2 * n
    out = bytearray(b"BAM V1  " + struct.pack("<HBBIII", n, 1, 0, frames_off, pal_off, lut_off))
    for i in range(n):
        out += struct.pack("<HHhhI", w, h, 0, 0, (data_off + i * w * h) | 0x80000000)
    out += struct.pack("<HH", n, 0)
    out += bytes((0, 255, 0, 0))
    for i in range(255):
        r, g, b = pal[i * 3:i * 3 + 3]
        out += bytes((b, g, r, 0))
    out += b"".join(struct.pack("<H", i) for i in range(n))
    for i in range(n):
        out += idx[:, i * w:(i + 1) * w].tobytes()
    path.write_bytes(bytes(out))


def end_turn(size=72):
    img = Image.open(ROOT / "graphics" / "end_turn.jpg").convert("RGB")
    # the round disc (rim included) fills the middle ~70 % of the square plate
    s = img.size[0]
    m = int(s * 0.15)
    img = img.crop((m, m, s - m, s - m)).resize((size, size), Image.LANCZOS)
    img = ImageEnhance.Sharpness(img).enhance(1.6)
    # the plate is dark bronze: at 72 px it reads grey on the bar, so lift it
    img = ImageEnhance.Contrast(img).enhance(1.35)
    img = ImageEnhance.Brightness(img).enhance(1.3)
    img = ImageEnhance.Color(img).enhance(1.25)
    yy, xx = np.mgrid[0:size, 0:size]
    r = size / 2
    inside = ((xx + 0.5 - r) ** 2 + (yy + 0.5 - r) ** 2) <= (r - 0.5) ** 2
    def rgba(im):
        a = np.array(im, np.uint8)
        alpha = np.where(inside, 255, 0).astype(np.uint8)
        return np.dstack([a, alpha])
    normal = rgba(img)
    lit = rgba(ImageEnhance.Brightness(img).enhance(1.35))
    grey = rgba(ImageEnhance.Brightness(ImageEnhance.Color(img).enhance(0.0)).enhance(0.6))
    write_bam_v1(OUT / "PFENDTRN.BAM", [normal, lit, lit, grey])


def page_arrow(name, up, w=22, h=36):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32) + 0.5
    def frame(gold, plate, edge):
        img = np.zeros((h, w, 4), np.uint8)
        img[..., :3] = plate
        img[..., 3] = 255
        border = (xx < 2) | (xx > w - 2) | (yy < 2) | (yy > h - 2)
        img[border, :3] = edge
        # the triangle: apex at a third of the height, base at two thirds
        cy = h / 2
        tip, base = (cy - 7, cy + 5) if up else (cy + 7, cy - 5)
        t = (yy - base) / (tip - base)
        inside = (t >= 0) & (t <= 1) & (np.abs(xx - w / 2) <= (1 - t) * 7.5)
        img[inside, :3] = gold
        return img
    normal = frame((236, 196, 80), (30, 24, 16), (92, 72, 40))
    lit = frame((255, 226, 120), (52, 40, 24), (150, 118, 60))
    grey = frame((90, 84, 76), (26, 24, 22), (60, 56, 50))
    write_bam_v1(OUT / name, [normal, lit, lit, grey])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gemrb", default=r"D:\gemrb")
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    end_turn()
    page_arrow("PFPAGEUP.BAM", True)
    page_arrow("PFPAGEDN.BAM", False)
    src = Path(a.gemrb) / "gemrb" / "override" / "pst"
    for name, out in (("tbtrip.bam", "PFTRIP.BAM"), ("tbwithdr.bam", "PFWITHDR.BAM"), ("tbrun.bam", "PFSPRINT.BAM")):
        shutil.copyfile(src / name, OUT / out)
    for p in sorted(OUT.glob("*.BAM")):
        print(p.name, p.stat().st_size)


if __name__ == "__main__":
    main()
