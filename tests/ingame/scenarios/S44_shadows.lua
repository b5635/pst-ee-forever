-- scenario: save=000700196 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S44 Fortress of Regrets shadows (AR1201): the Nameless One alone against 13 Greater Shadows. The fight ends (he dies
-- and gets up, or they die), no endless spawns, no stuck turn.
S.steps[6] = function() PSTF_SetHp(S.tno(), 300, 300) end
S.brawl{name = 'Greater Shadow', provoke = false, rounds = 4}
