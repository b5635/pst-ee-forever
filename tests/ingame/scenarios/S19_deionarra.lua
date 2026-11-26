-- scenario: save=000700195 env=PSTF_TBC_AUTOPARTY=1 timeout=500
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S19 Deionarra (1200DEIO, AR1200): attacked -> PlaySequence(5), Wait(3), GLOBAL Game_Over = 3, DestroySelf (the game ends: the scenario stops at the variable).
S.plot{name = 'Deionarra', cre = 'DEIONS', spawn = true, script = '1200DEIO', provoke = true, expect = {{'Game_Over', 'GLOBAL', 3}}, maxFrames = 4000}
