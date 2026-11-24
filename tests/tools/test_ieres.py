# SPDX-License-Identifier: GPL-2.0-or-later
"""ieres.py resolves resources by their KEY type code (IESDP general.htm). Needs the installed game."""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def find(regex, ext):
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "ieres.py"), "find", regex, ext],
                         capture_output=True, text=True, check=True).stdout
    return out.split()


def test_type_codes_match_iesdp():
    # 0x3e9 is WED (it was mislabelled WAV), 0x001 BMP, 0x405 GLSL, 0x3fc VEF
    assert find("^AR0202$", "WED") == ["AR0202.WED"]
    assert find("^AR0202$", "WAV") == []
    assert find("^AR0202LM$", "BMP") == ["AR0202LM.BMP"]
    assert find("^FPSPRITE$", "GLSL") == ["FPSPRITE.GLSL"]


def test_area_count_matches_lighting_data():
    # 123 WEDs, one LM light map each (same set as the GemRB lighting pipeline, docs/VISUALS_PLAN.md)
    assert len(find(".", "WED")) == 123
    assert len(find("LM$", "BMP")) == 123


def test_party8_save_portraits_shipped():
    # CInfGame::SaveGame writes PORTRT<slot>.BMP into existing resources only (dimmGetResObject(.., create=0),
    # lin 3628A0): the game ships PORTRT0..5, so slots 6..7 need ours or the save faults in CRes::Write.
    assert set("PORTRT%d.BMP" % i for i in range(6)) <= set(find("^PORTRT[0-9]$", "BMP"))
    ours = os.path.join(ROOT, "data", "override")
    for i in (6, 7):
        p = os.path.join(ours, "PORTRT%d.BMP" % i)
        assert os.path.getsize(p) == 3128 and open(p, "rb").read(2) == b"BM"
