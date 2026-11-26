-- scenario: save=000700077 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S16 Rat Warren team-2 leader (1600T2LD on a spawned SPELRAT, AR1600): one response of 9 Spell/Wait(5) pairs
-- (2301 2301 2212 2207 2207 2115 2107 2107 2101) behind Range([EA=2],32): one spell per turn, the chain goes on
-- across turns (keep_queue), spells it lacks fall through; then LOCALS cd_int_1 = 1. The Leaving_2 global
-- timer path (30 s) runs on world time.
S.chainCaster{name = 'SPELRAT', cre = 'SPELRAT', spawn = true, script = '1600T2LD', casts = 4, turns = 10,
	set = {{'AR1600_PC_Enemy', 'GLOBAL', 1}}, hp = 400, maxFrames = 12000}
