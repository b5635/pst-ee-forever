-- scenario: save=000700189 env=PSTF_TBC_AUTOPARTY=1 timeout=900
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S40 Trias (AR0901, as the walkthrough plays it): with Trias_Sword >= 2 an attack on him is a fight
-- ("AttackedBy & GlobalGT(Trias_Sword,1) => Enemy(); Attack"), below 2 it is Game_Over 3. Turn-based must keep the
-- fight branch: no Game_Over, the fight runs (round 4 begins) with no stuck turn.
-- b17: Trias_Sword 2 also runs his "Global(Trias_Sword,2) => Dialogue([EA=2])" block (a 2-reply dialogue holds the world):
-- 3 keeps the fight branch (GlobalGT 1) without it.
-- b16: the protagonist's attack opens Trias' dialogue (no hit; Game_Over stayed 0): made hostile instead.
S.clickReplies = true  -- the save opens in the Trias talk (2-reply nodes): click through it
S.brawl{name = 'Trias', provoke = false, hostile = true, hp = 200, rounds = 4, partyHp = true, set = {{'Trias_Sword', 'GLOBAL', 3}}, maxFrames = 9000}
local inner = S.tick
local over = false
S.tick = function(frame, cur, started)
	if not over and frame > 30 and frame % 15 == 0 then
		for v = 1, 3 do
			if S.global('Game_Over', 'GLOBAL', v) then over = true S.issue('Game_Over = ' .. v .. ' at tick ' .. frame) end
		end
	end
	return inner(frame, cur, started)
end
