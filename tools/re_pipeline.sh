#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# RE pipeline for the current build (machine time, not tokens):
#   1. IDBs: Linux (named) and Windows (hook target); recover loose code as functions (fixfuncs.py)
#   2. features.py on both, vtscan.py on Windows (no RTTI)
#   3. Diaphora: export Linux + a fresh *unnamed* Windows IDB (independent evidence), diff them
#   4. match.py: custom-only run (cross-validation copy) then merged run (Diaphora + custom evidence)
#   5. full_dump.py on both (apply names, every function: pseudocode/asm/tables, verify.json)
#   6. re.py build (SQLite index)
# Usage: bash tools/re_pipeline.sh [fresh]      fresh = delete IDBs and re-analyse from the binaries
#        FROM=diff bash tools/re_pipeline.sh  resume at the Diaphora diff (exports exist); FROM=match, FROM=dump
# Then:  python -m pytest tests/tools -q         (asserts completeness)
set -u
IDA="/c/Program Files/IDA Professional 9.1/idat.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; RE="$ROOT/resources/re"; DIA="$ROOT/resources/ext/diaphora"
W() { cygpath -w "$1"; }
log() { echo "[$(date +%H:%M:%S)] $*"; }
ida() { local dir=$1 db=$2 logf=$3; shift 3; ( cd "$dir" && "$IDA" -A -L"$logf" "$@" "$db" ); }
dia_export() { # dir db out.sqlite
  ( cd "$1" && DIAPHORA_AUTO=1 DIAPHORA_USE_DECOMPILER=1 DIAPHORA_EXPORT_FILE="$(W "$3")" \
      "$IDA" -A -L"diaphora_export.log" -S"$(W "$DIA/diaphora.py")" "$2" ); }

FROM=${FROM:-start}
if [ "${1:-}" = "fresh" ]; then rm -f "$RE/lin/Torment.i64" "$RE/win/Torment.exe.i64"; fi
mkdir -p "$RE/diaphora/win"
LDB=Torment; [ -f "$RE/lin/Torment.i64" ] && LDB=Torment.i64
WDB=Torment.exe; [ -f "$RE/win/Torment.exe.i64" ] && WDB=Torment.exe.i64

if [ "$FROM" = start ]; then
( ida "$RE/lin" $LDB fixfuncs.log -S"$(W "$RE/fixfuncs.py")" && log "lin fixfuncs" \
  && ida "$RE/lin" Torment.i64 features.log -S"$(W "$RE/features.py")" && log "lin features" \
  && dia_export "$RE/lin" Torment.i64 "$RE/diaphora/lin.sqlite" && log "lin diaphora export" ) &
( ida "$RE/win" $WDB fixfuncs.log -S"$(W "$RE/fixfuncs.py")" && log "win fixfuncs" \
  && ida "$RE/win" Torment.exe.i64 features.log -S"$(W "$RE/features.py")" \
  && ida "$RE/win" Torment.exe.i64 vtscan.log -S"$(W "$RE/vtscan.py")" && log "win features + vtscan" ) &
( cp "$RE/win/Torment.exe" "$RE/diaphora/win/" && rm -f "$RE/diaphora/win/Torment.exe.i64" \
  && ida "$RE/diaphora/win" Torment.exe fixfuncs.log -S"$(W "$RE/fixfuncs.py")" \
  && dia_export "$RE/diaphora/win" Torment.exe.i64 "$RE/diaphora/win.sqlite" && log "win diaphora export" ) &
wait
fi
if [ "$FROM" = start ] || [ "$FROM" = diff ]; then
# Diaphora imports its own modules from its folder, so no -I here (trusted, owner-approved tool)
python "$DIA/diaphora.py" "$(W "$RE/diaphora/lin.sqlite")" "$(W "$RE/diaphora/win.sqlite")" \
  -o "$(W "$RE/diaphora/lin_vs_win.diaphora")" > "$RE/diaphora/diff.log" 2>&1 && log "diaphora diff" || { log "diaphora diff FAILED"; tail -5 "$RE/diaphora/diff.log"; }
fi
if [ "$FROM" != dump ]; then
NO_DIAPHORA=1 python -I "$RE/match.py" | tail -3
python -I "$RE/match.py" | tail -22 || exit 1
fi

ida "$RE/lin" Torment.i64 full_dump.log -S"$(W "$RE/full_dump.py")" && log "lin full dump" &
ida "$RE/win" Torment.exe.i64 full_dump.log -S"$(W "$RE/full_dump.py")" && log "win full dump" &
wait
python -I -c "import json,sys;[print(s, {k:v for k,v in json.load(open(sys.argv[1]+'/'+s+'/dump/verify.json')).items() if k!='orphan_sample'}) for s in ('lin','win')]" "$RE"
python -I "$ROOT/tools/re.py" build && log "index built"
