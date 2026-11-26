-- scenario: save=000700083 env=PSTF_TBC_AUTOPARTY=1 timeout=700
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S47 Morte dies mid-fight (HP 1, AR0403): he leaves the fight, the turn order goes on, no stuck turn.
S.steps[200] = function() local m = S.find('Morte') if m ~= -1 then PSTF_SetHp(m, 1, 1) S.note(string.format('Morte %X at 1 HP', m)) end end
S.brawl{name = 'Starved Dog Barking Thug', provoke = false, maxFrames = 6000}
