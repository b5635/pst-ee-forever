# SPDX-License-Identifier: GPL-2.0-or-later
"""Armor value (hit sound set) of every PST creature animation, for the gore module.

  python -I tools/hitmat.py            write data/hitarmor.tsv from the game's RESDATA.INI ("anim armor")
  python -I tools/hitmat.py show       print anim, armor, material (data/hitmat.2da rules) and name

RESDATA.INI sections are [N] = the animation id (EE goes past 255, e.g. [558]), `armor=` the HIT_0<type><A+armor> sound set
(GemRB ScenePolish::MaterialOf reads the same key). Re-run after a mod changes RESDATA.INI.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(HERE, "sprite_depth"))
sys.path.append(HERE)
import sprite_depth as sd  # noqa: E402  (RESDATA reader)

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "data", "hitarmor.tsv")
MATERIALS = {0: "FLESH", 1: "LEATHER", 2: "ARMORED", 3: "STONE", 4: "BONE", 5: "FLESH", 6: "SPIRIT", 7: "METAL",
             8: "FLESH", 9: "SHADOW", 10: "FIRE"}


def parse(ini: str) -> list[tuple[int, int, str]]:
  """(anim, armor, comment name) for every section with an armor key."""
  out, sec, name, armor = [], None, "", None
  def flush():
    if sec is not None and armor is not None:
      out.append((sec, armor, name))
  for raw in ini.splitlines():
    m = re.match(r"\s*\[(\d+)\]\s*(?://\s*(.*))?", raw)
    if m:
      flush()
      sec, name, armor = int(m.group(1)), (m.group(2) or "").strip(), None
      continue
    m = re.match(r"\s*armor\s*=\s*(\d+)", raw, re.I)
    if m and sec is not None:
      armor = int(m.group(1))
  flush()
  return out


def table_rows(path: str) -> tuple[dict[int, str], dict[int, str]]:
  anim, armor = {}, {}
  for line in open(path).read().splitlines()[3:]:
    parts = line.split()
    if len(parts) < 2:
      continue
    if parts[0].upper().startswith("ANIM"):
      anim[int(parts[0][4:])] = parts[1].upper()
    elif parts[0].upper().startswith("ARMOR"):
      armor[int(parts[0][5:])] = parts[1].upper()
  return anim, armor


def main(argv: list[str]) -> int:
  rows = parse(sd.read_res("RESDATA", "0x802").decode("latin1"))
  if argv and argv[0] == "show":
    anim, armor = table_rows(os.path.join(ROOT, "data", "hitmat.2da"))
    for a, arm, name in rows:
      print(f"{a:3d} armor {arm:2d} {anim.get(a) or armor.get(arm) or MATERIALS.get(arm, 'FLESH'):12s} {name}")
    return 0
  with open(OUT, "w", encoding="ascii", newline="\n") as f:
    f.write("# SPDX-License-Identifier: GPL-2.0-or-later\n"
            "# Written by tools/hitmat.py from RESDATA.INI: animation id (low byte), armor (hit sound set), name.\n")
    for a, arm, name in rows:
      f.write(f"{a}\t{arm}\t# {name.encode('ascii', 'replace').decode()}\n")
  print(f"hitmat: {len(rows)} animations -> {OUT}")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
