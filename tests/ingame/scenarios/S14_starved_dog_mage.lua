-- scenario: save=000700083 env=PSTF_TBC_AUTOPARTY=1 timeout=400
-- SPDX-License-Identifier: GPL-2.0-or-later
-- S14 Starved Dog Mage (0403MAGE, AR0403 Alley of Dangerous Angles): a step-counter caster. Each step is
-- "Spell(...); Wait(N); IncrementGlobal("MortalKombat","AR0403",1)". Expected in turn-based: one spell per turn and
-- the counter climbs 1 -> 2 -> 3 -> 4 -> 5 (then it attacks). Audit risk: the turn ends at the Wait and ClearAI drops
-- the IncrementGlobal, so the counter stays at 1 and the mage repeats its first step (or melees) forever.
local mage
local seenStep = {}
local casts = 0
S.maxFrames = 4500
-- The save's own mage is dead (party killed the Starved Dogs' mage earlier): spawn SDMAGE and give it the area
-- actor's script (the CRE has none; AR0403 assigns 0403MAGE to its placed actor).
S.steps[2] = function() PSTF_EvalAs(S.tno(), 'SetGlobal("MortalKombat","AR0403",1)') end
S.steps[3] = function() S.spawn('SDMAGE', 220, 40) end
S.steps[5] = function()
	mage = S.find('Starved Dog Mage')
	if mage == -1 then return S.fail('SDMAGE did not spawn') end
	S.note(string.format('mage %X', mage))
	S.watch[mage] = true
	PSTF_EvalAs(mage, 'ChangeAIScript("0403MAGE",0)', 1)
end
S.steps[6] = function()
	PSTF_SetHp(mage, 400, 400)  -- lives long enough for every step
	for i = 0, 5 do local p = PSTF_PartyId(i) if p and p ~= -1 then PSTF_SetHp(p, 400, 400) end end
	if not S.global('MortalKombat', 'AR0403', 1) then return S.fail('could not set MortalKombat to 1') end
	PSTF_MakeEnemy(mage)
end
function S.tick(frame, cur)
	if not mage or frame < 8 then return end
	for v = 1, 6 do
		if not seenStep[v] and S.global('MortalKombat', 'AR0403', v) then
			seenStep[v] = frame
			S.note('MortalKombat = ' .. v .. ' at tick ' .. frame)
		end
	end
	if seenStep[5] then return S.finish('the mage went through all its steps') end
	if frame == 4400 then
		local top = 0
		for v = 1, 6 do if seenStep[v] then top = v end end
		S.issue('step counter stuck at ' .. top .. ' (expected 5 after its casting turns)')
	end
end
