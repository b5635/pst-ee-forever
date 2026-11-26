-- scenario: save=000700083 env=PSTF_TBC_AUTOPARTY=1 timeout=700
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S46 The Nameless One dies mid-fight (HP 1, Starved Dogs, AR0403): Mortuary area change, the fight ends, nobody held, no fault.
S.steps[200] = function() PSTF_SetHp(S.tno(), 1, 1) S.note('protagonist at 1 HP') end
S.brawl{name = 'Starved Dog Barking Thug', provoke = false, maxFrames = 6000}
