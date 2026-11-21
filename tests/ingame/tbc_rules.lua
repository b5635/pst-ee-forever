-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based effect rules in a real fight (Dead Nations save):
--   * the Nameless One (CON 20: 1 HP per 75 ticks) regains exactly 1 HP at the start of his turn and
--     never otherwise;
--   * Blur (SPWI216) cast by party member 2 counts down 75 ticks (one round) per own turn start and
--     stays the same in between;
--   * Adder's Kiss (SPWI201) by party member 3: the enemy loses HP only during the caster's turn (the
--     first hit) or right at the start of its own turn (the delayed hits);
--   * the caster's turn does not end before its spell has resolved (no projectile of it left).
-- Run: tools\launch.ps1 -Lua tests\ingame\tbc_rules.lua -Env @{PSTF_TEST_TICK='1'} -Until 'TEST PASS|TEST FAIL' -Close -Timeout 400
local tno, a, b = PSTF_PartyId(0), PSTF_PartyId(1), PSTF_PartyId(2)
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
-- the strongest creature close to the party becomes the enemy
local enemy = 0x320032
if os_con20 then C:Eval('ChangeStat(Myself,41,20,1)', 0) end
local frames, cur, turnStart, round = 0, -1, 0, 0
local started = false
local acted = {}
local lastHp = {}
local blurAt = {}    -- Blur ticks left at each of a's turn starts
local regen = {}     -- TNO HP gained per own turn
local tnoTurnHp = nil
local issued = -1
local waitFrames = 0
local retried = false
local adderTurn = -1
local adderHit = false
local function window(id) return cur == id and frames - turnStart <= 45 end
function pstfTestTick()
	frames = frames + 1
	if frames == 3 then C:Eval('ApplyDamage(Myself,25,0)', 0) end
	if frames == 8 then
		PSTF_MakeEnemy(enemy)
		PSTF_Note('enemy hp ' .. hp(enemy) .. ', tno hp ' .. hp(tno))
	end
	local id, r, party, used, acting, ft = PSTF_TbcCurrent()
	if id ~= -1 then started = true end
	if started and id == -1 then
		if round >= 4 then
			PSTF_Note('fight ended at round ' .. round)
		else
			return fail('fight ended early at round ' .. round)
		end
	end
	if id ~= cur then
		cur, turnStart, round = id, frames, r
		retried = false
		issued, waitFrames = -1, 0
		if id == a then blurAt[#blurAt + 1] = -2 end  -- read a few frames in (after the step)
		PSTF_Note(string.format('turn %d round %d: tno %d, enemy %d', id, r, hp(tno), hp(enemy)))
	end
	-- HP watch: who changed when
	for _, w in ipairs({tno, enemy}) do
		local h = hp(w)
		if started and lastHp[w] and h ~= lastHp[w] then
			PSTF_Note(string.format('hp %d %d -> %d (turn of %d, %d frames in, round %d)', w, lastHp[w], h, cur, frames - turnStart, round))
			if w == tno and h > lastHp[w] and lastHp[w] > 0 then
				if not window(tno) then return fail('TNO regenerated outside the start of his turn') end
				regen[#regen + 1] = h - lastHp[w]
			end
			if w == enemy and h < lastHp[w] and cur == a and acted.adder then adderHit = true end
			if w == enemy and h < lastHp[w] and h > 0 then
				if not (cur == a or cur == b or window(enemy)) then return fail('enemy lost HP outside the caster turn and its own turn start') end
			end
		end
		lastHp[w] = h
	end
	if id == a and blurAt[#blurAt] == -2 and frames - turnStart == 3 then
		blurAt[#blurAt] = PSTF_HasEffect(a, 65, 1)
		PSTF_Note('blur left ' .. blurAt[#blurAt])
	end
	if id ~= -1 and party and frames - turnStart > 4 then
		if issued == -1 then
			if id == a and not acted[a] then C:Eval('ForceSpell(Myself,2216)', 1) acted[a] = true issued = frames
			elseif id == a and acted[a] and not acted.adder then C:Eval('ForceSpell(NearestEnemyOf(Myself),2201)', 1) acted.adder = true issued = frames adderTurn = frames
			elseif id == b and not acted[b] then C:Eval('Attack(NearestEnemyOf(Myself))', 2) acted[b] = true issued = frames
			else issued = 0 end
		elseif issued > 0 then
			if (frames - issued) % 6 == 0 and frames - issued <= 90 then
				local n = PSTF_Objects(id)
				if n > 0 then PSTF_Note('caster ' .. tostring(id) .. ' objects: ' .. n .. ' at +' .. (frames - issued)) end
			end
			-- NearestEnemyOf needs the caster to have seen the enemy: retry with the EA object
			if id == a and acted.adder and not used and frames - issued == 30 and not retried then
				retried = true
				PSTF_Note('Adder retry with [ENEMY]') C:Eval('ForceSpell([ENEMY],2201)', 1) issued = frames
			end
		end
		if issued ~= -1 and not acting and (issued == 0 or frames - issued > 45) then
			waitFrames = waitFrames + 1
			if waitFrames > 8 then PSTF_EndTurn() end
		end
	end
	if round >= 7 then
		-- Blur: 75 ticks less per own turn start
		for i = 3, #blurAt do
			if blurAt[i] >= 0 and blurAt[i - 1] >= 0 and blurAt[i - 1] - blurAt[i] ~= 75 then
				return fail('blur went ' .. blurAt[i - 1] .. ' -> ' .. blurAt[i] .. ' between turns')
			end
		end
		if acted.adder and not adderHit then return fail("Adder's Kiss did not hit in the caster's own turn") end
		for _, g in ipairs(regen) do if g ~= 1 then return fail('TNO regenerated ' .. g .. ' at once') end end
		PSTF_Note('TEST PASS: regen ' .. #regen .. ' x 1 HP, adder hit in turn ' .. tostring(adderHit) .. ', blur ' .. table.concat(blurAt, ','))
		pstfTestTick = nil
	end
end
