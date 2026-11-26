-- scenario: save=000700083 env=PSTF_TBC_AUTOPARTY=1 timeout=700
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S49 A Starved Dog thug charmed mid-fight (op 5, 3 rounds): changes sides (tbc: X changes sides), fights for the party, goes back when it ends.
S.steps[200] = function() if S.caster then PSTF_StatusFxApply(S.caster, 5, 0, 0, 15) S.note('charmed') end end
S.steps[700] = function() if S.caster then PSTF_Side(S.caster) end end
S.brawl{name = 'Starved Dog Barking Thug', provoke = false, maxFrames = 6000}
