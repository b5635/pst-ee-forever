# SPDX-License-Identifier: GPL-2.0-or-later
"""Contact sheet of tests/ingame/statusfx_look.lua shots: one row per effect, crops round the screen centre.

  python -I tools/statusfx_sheet.py [--crop W,H] [--out PATH]
"""
import glob, os, re, sys

from PIL import Image, ImageDraw

LOGS = r"C:\GOG Games\Planescape Torment - Enhanced Edition\pst_forever\logs"
ORDER = ["fear", "stun", "stunloop", "blind", "acid", "sonic", "mantle"]


def main(a):
  cw, ch = 360, 300
  if "--crop" in a: cw, ch = map(int, a[a.index("--crop") + 1].split(","))
  out = a[a.index("--out") + 1] if "--out" in a else os.path.join(LOGS, "sfxlook_sheet.png")
  rows = []
  for fx in ORDER:
    files = sorted(glob.glob(os.path.join(LOGS, f"sfxlook_{fx}_*.bmp")), key=lambda f: int(re.findall(r"_(\d+)\.bmp$", f)[0]))
    if files: rows.append((fx, files))
  if not rows: sys.exit("no sfxlook shots")
  ncol = max(len(f) for _, f in rows)
  sheet = Image.new("RGB", (90 + ncol * cw, len(rows) * ch), (0, 0, 0))
  d = ImageDraw.Draw(sheet)
  for r, (fx, files) in enumerate(rows):
    d.text((4, r * ch + 4), fx, fill=(255, 255, 255))
    for c, f in enumerate(files):
      im = Image.open(f).convert("RGB")
      x0, y0 = im.width // 2 - cw // 2, im.height // 2 - ch * 2 // 3
      sheet.paste(im.crop((x0, y0, x0 + cw, y0 + ch)), (90 + c * cw, r * ch))
  sheet.save(out)
  print(out, sheet.size)


if __name__ == "__main__":
  main(sys.argv[1:])
