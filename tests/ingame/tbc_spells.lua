-- SPDX-License-Identifier: GPL-2.0-or-later
-- Spells in turn-based combat (Dead Nations save). Party member 2 (a mage) casts one spell per own
-- turn at the enemy: Adder's Kiss SPWI201, Ax of Torment SPWI310, Ice Knife SPWI207, Cone of Cold
-- SPWI502, Cloudkill SPWI501. Nobody else attacks the enemy, so every HP loss it takes must come
--   * during the caster's turn (the spell resolves before the turn ends: the turn waits for it), or
--   * right at the start of its own turn (damage over time: Adder's Kiss' later hits).
-- Run: tools\launch.ps1 -Lua tests\ingame\tbc_spells.lua -Env @{PSTF_TEST_TICK='1'} -Until 'TEST PASS|TEST FAIL' -Close -Timeout 900
local P = {PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)}
local caster = P[2]
local enemy = 0x320032
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local spells = {{'IceKnife', 2207}, {'ConeOfCold', 2502}, {'Ax', 2310}, {'Adder', 2201}, {'Cloudkill', 2501}}
local frames, cur, turnStart, round = 0, -1, 0, 0
local started, nextSpell, issued = false, 1, -1
local seen = {}
local results = {}
local dot = 0
function pstfTestTick()
	frames = frames + 1
	if frames == 8 then PSTF_MakeEnemy(enemy) end
	local id, r, party, used, acting = PSTF_TbcCurrent()
	if id ~= -1 then started = true end
	if started and id == -1 then
		if nextSpell > #spells then PSTF_Note('fight over after all casts') else return fail('fight ended before all casts (round ' .. round .. ')') end
	end
	if id ~= cur then
		cur, turnStart, round, issued = id, frames, r, -1
		if id == caster or id == enemy then PSTF_Note(string.format('turn %d round %d: enemy hp %d', id, r, hp(enemy))) end
	end
	-- every hostile combatant: HP lost by spells only in the caster's turn or at its own turn start
	-- (melee from party members never happens in this test: they only cast or wait)
	local i = 1
	while true do
		local w = PSTF_FightId(i)
		if w == -1 then break end
		i = i + 1
		if w ~= P[1] and w ~= P[2] and w ~= P[3] then
			local hw = hp(w)
			if started and seen[w] and hw < seen[w] then
				local k = frames - turnStart
				local c = results[#results]
				PSTF_Note(string.format('%d: %d -> %d (turn of %d, %d frames in, round %d)', w, seen[w], hw, cur, k, round))
				if cur == caster and c and c.round == round then
					c.hit = true
				elseif cur == w and k <= 45 then
					dot = dot + 1
				else
					return fail(w .. ' lost HP outside the caster turn and its own turn start')
				end
			end
			seen[w] = hw
		end
	end
	local h = hp(enemy)
	if h <= 0 and started then
		PSTF_Note('enemy died')
	end
	if id == -1 then return end
	if party then
		if id == caster and frames - turnStart == 3 then PSTF_SetHp(enemy, 64) seen[enemy] = 64 end
		if id == caster and issued == -1 and frames - turnStart == 5 and nextSpell <= #spells and h > 0 then
			local s = spells[nextSpell]
			-- ReallyForceSpellLevel: the SPL headers need caster level 5 (Ax) / 9 (Cloudkill, Cone of Cold)
			C:Eval('ReallyForceSpellLevel(NearestEnemyOf(Myself),' .. s[2] .. ',12)', 1)
			results[#results + 1] = {name = s[1], round = round, hit = false}
			PSTF_Note('cast ' .. s[1] .. ' in round ' .. round)
			nextSpell = nextSpell + 1
			issued = frames
		end
		local waited = issued == -1 and frames - turnStart > 10 or (issued ~= -1 and frames - issued > 40)
		if waited and not acting then PSTF_EndTurn() end
	end
	if nextSpell > #spells and results[#results].round < round or (h <= 0 and started) then
		local bad = {}
		for _, c in ipairs(results) do
			PSTF_Note(string.format('result %s: hit in its caster turn %s', c.name, tostring(c.hit)))
			if not c.hit then bad[#bad + 1] = c.name end
		end
		-- Spells can miss (Ice Knife rolls to hit) or meet immune undead (Cloudkill's poison and death):
		-- the invariant is that nothing lands outside its time; at least three must have hit in turn.
		if #results - #bad < 3 then return fail('too few spells hit in their caster turn: missing ' .. table.concat(bad, ', ')) end
		PSTF_Note('TEST PASS: ' .. #results .. ' spells resolved in their caster turn, ' .. dot .. ' damage-over-time hits at the enemy turn start')
		pstfTestTick = nil
	end
end
