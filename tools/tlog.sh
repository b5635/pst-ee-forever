#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Short view of the game log after a test: notes, turn starts/ends, AI orders, rolls, faults.
#   bash tools/tlog.sh [extra grep -E pattern]
L="/c/GOG Games/Planescape Torment - Enhanced Edition/pst_forever/logs/pst_forever.log"
P='lua note|idle on|fight [0-9]+ (starts|over)|joins|leaves|faulted|CRASH|test tick error|roll:|stop \('
[ -n "$1" ] && P="$P|$1"
grep -E "$P" "$L" | cut -c1-190
