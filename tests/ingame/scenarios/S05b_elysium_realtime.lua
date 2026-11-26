-- scenario: save=000700145 env=PSTF_TBC_NOFIGHT=1,PSTF_FEEDBACK_LOG=1 timeout=300
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S05b A/B for S05: Lady Thorncombe's Elysium's Tears (SPWI305) did no damage in turn-based. Here the same cast in
-- real time (no fights), 5 times at Morte: each meteor rolls to hit with her THAC0 vs Morte's AC
-- (CPlanescape::ProjectileRollToHit), so some casts may miss with every meteor. Notes the damage per cast.
-- Result (2026-11-26): 3 of 5 casts went off (feedback 5), Morte lost 0 HP each: same as turn-based, vanilla.
-- ChangeStat ARMORCLASS on a party member did not take (AC stayed 0).
local lady, morte, hp0, casts, dmg = nil, nil, nil, 0, {}
S.maxFrames = 2400
S.steps[3] = function() PSTF_EvalAs(lady, 'JumpToObject("Morte")', 1) end
S.steps[2] = function()
	lady = S.find('Lady Thorncombe')
	morte = S.find('Morte')
	if lady == -1 or morte == -1 then return S.fail('Thorncombe or Morte not found') end
	PSTF_SetHp(morte, 400, 400)
	PSTF_EvalAs(morte, 'ChangeStat(Myself,ARMORCLASS,10,SET)', 1)  -- meteors hit on d20 >= THAC0 - 10
	local t1, a1 = PSTF_Stats(lady)
	local t2, a2 = PSTF_Stats(morte)
	S.note(string.format('Thorncombe THAC0 %d, Morte AC %d: a meteor hits on d20 >= %d', t1, a2, t1 - a2))
end
function S.tick(frame)
	if not lady or lady == -1 or frame < 10 then return end
	if frame % 400 == 10 then
		if casts > 0 then
			local _, _, h = PSTF_Stats(morte)
			dmg[#dmg + 1] = (hp0 or h) - h
			S.note(string.format('cast %d: Morte lost %d HP', casts, dmg[#dmg]))
		end
		if casts == 5 then return S.finish('real-time damage per cast: ' .. table.concat(dmg, ',')) end
		_, _, hp0 = PSTF_Stats(morte)
		casts = casts + 1
		PSTF_EvalAs(lady, 'SpellNoDec("Morte",2305)', 1)
	end
	if frame % 400 == 40 then
		PSTF_Action(lady)
		local _, a = PSTF_Stats(morte)
		S.note(string.format('cast %d: her action logged, %d casts so far, Morte AC %d', casts, PSTF_Casts(lady) or -1, a))
	end
end
