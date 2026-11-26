-- scenario: save=000700083 env=PSTF_TBC_AUTOPARTY=1 timeout=700
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S50 A Starved Dog thug made invisible mid-fight (op 20): its turns still run (the nudge orders Attack(PlayerN), no sight needed); no stuck turn.
S.steps[200] = function() if S.caster then PSTF_StatusFxApply(S.caster, 20, 0, 0, 30) S.note('invisible') end end
S.brawl{name = 'Starved Dog Barking Thug', provoke = false, maxFrames = 6000}
