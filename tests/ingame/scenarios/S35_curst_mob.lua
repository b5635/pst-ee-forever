-- scenario: save=000700186 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S35 Curst mob (AR0900, 0900MOB on CANLEAD/CRSTCOS*): a big town crowd turns on the party. Playable: rounds go by,
-- no stuck turn, no endless round (this save has 16 citizens, 11 guards, slavers, looters).
-- No script name to provoke it by (Attack("None")): its own AttackedBy response is played instead (made hostile = Enemy(), then Help()).
S.steps[31] = function() PSTF_EvalAs(S.caster, 'Help()') S.note('its AttackedBy response: Enemy() + Help()') end
S.brawl{name = 'Curst Citizen', provoke = false, hostile = true, hp = 40, rounds = 3}
