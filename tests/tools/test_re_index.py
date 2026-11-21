# SPDX-License-Identifier: GPL-2.0-or-later
"""Guards for the RE dumps and index (python -m pytest tests/tools -q). Target: GOG PST:EE 3.2.2.0."""
import hashlib, json, os, sqlite3, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RE_DIR = os.path.join(ROOT, "resources", "re")
DB = os.path.join(RE_DIR, "index.sqlite")
GAME = r"C:\GOG Games\Planescape Torment - Enhanced Edition"
WIN_SHA = "99ae15860cfdbcdaad0f48e005eee21444046b21de848c1deab9367e8246a3c5"
LIN_SHA = "e615763edb57427e3efded67296c357e2563ee40ebc2aef929e1f180e1b5846e"


def q(sql, *a):
    return sqlite3.connect(DB).execute(sql, a).fetchall()


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def verify(side):
    return json.load(open(os.path.join(RE_DIR, side, "dump", "verify.json")))


def test_reference_binaries_are_the_target_build():
    assert sha(os.path.join(RE_DIR, "win", "Torment.exe")) == WIN_SHA
    assert sha(os.path.join(RE_DIR, "lin", "Torment")) == LIN_SHA


def test_installed_game_matches_reference():
    """Fails when the game updates: re-fingerprint and re-run tools/re_pipeline.sh."""
    if os.path.exists(os.path.join(GAME, "Torment.exe")):
        assert sha(os.path.join(GAME, "Torment.exe")) == WIN_SHA


def test_dumps_are_complete():
    for side in ("lin", "win"):
        v = verify(side)
        assert v["complete"], (side, v)
        n = v["ida_function_count"]
        assert v["listed_functions"] == v["all_c_entries"] == v["all_asm_entries"] == v["dec_all_files"] == n
        assert v["decompiled"] + v["hexrays_failed_with_disasm_fallback"] == n
        assert v["decompiled"] / n > 0.98, side  # failures keep their disassembly in all_Torment.c
        assert v["orphan_heads_dumped"] == v["code_heads_outside_functions"], side  # loose code -> orphans.asm


def test_index_holds_every_function():
    for side in ("lin", "win"):
        n = verify(side)["ida_function_count"]
        for table in ("func", "code", "asm"):
            assert q(f"select count(*) from {table} where bin=?", side)[0][0] == n, (side, table)


def test_core_functions_are_mapped():
    """Spot checks of string-anchored ports (re-verify after any re-match)."""
    for name in ("CInfGame::SynchronousUpdate", "CScreenWorld::StartDialog", "CGameAIBase::ExecuteAction"):
        lin = q("select ea from func where bin='lin' and base=?", name)
        assert len(lin) == 1, name
        assert q("select win from xmap where lin=?", lin[0][0]), name


def test_windows_names_are_applied_without_mangling():
    rows = q("select f.name from xmap x join func f on f.bin='win' and f.ea=x.win limit 200")
    assert rows and not any(n.startswith("_Z") for (n,) in rows)


def test_cli_runs():
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "re.py"), "find", "^CInfGame::SynchronousUpdate"],
                         capture_output=True, text=True, check=True).stdout
    assert "win 14" in out  # mapped to a Windows x64 address
