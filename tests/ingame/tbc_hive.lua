-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based spells and items as in a real game (Hive save 000000126): Dak'kon gets XP (real level-ups,
-- no forced caster level) and casts with SpellNoDec (PST:EE: needs his own level, not memorised);
-- items are given and used with UseItem. Three Hive thugs next to each other turn hostile.
--   turn 3 of Dak'kon: Swarm Curse (SPWI212) on the thugs: it pulses at most once per round per thug,
--                      and only in Dak'kon's own turns (lingering spell, paced by its caster's rounds);
--   turn 2: Adder's Kiss (SPWI201): its first hit lands in the turn it is cast;
--   turn 1: Whispering Flask (SPICE) on himself: the item takes his action; its visual (op369, 5 s
--           delay) plays now, not a round later;
--   turn 4: Chromatic Orb (SPWI101, save vs. spell): the save sound plays with the "Save vs." line
--           (check the log for 'tbc: save sound');
--   Morte's turns: Jar of Embalming Fluid (EMBALM) on himself: takes the action.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_hive.lua -Env @{PSTF_TEST_TICK='1'} -Until 'TEST PASS|TEST FAIL' -Close -Timeout 900
local function name(id)
	local _, _, _, _, ref = PSTF_SpritePos(id, 1)
	return ref and Infinity_FetchString(ref) or tostring(id)
end
local P, dak, morte = {}, -1, -1
for s = 0, 7 do
	local id = PSTF_PartyId(s)
	if id ~= -1 then P[#P + 1] = id end
end
local thugs = {0x180018, 0x140014, 0x1C001C}
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local frames, cur, turnStart, round = 0, -1, 0, 0
local started, dakTurns, morteTurns, issued = false, 0, 0, -1
local seen, pulses = {}, {}  -- pulses[thug][round] = damage events in Dak'kon's turns
local adderRound, adderHit, adderSnap = -1, false, nil
local itemTurns = {}
local plan = {'flask', 'adder', 'swarm', 'orb'}  -- Adder before the swarm: its hit is told apart
function pstfTestTick()
	frames = frames + 1
	if frames == 2 then
		for _, id in ipairs(P) do
			local n = name(id)
			if n:find('Dak') then dak = id end
			if n:find('Morte') then morte = id end
		end
		PSTF_Note(string.format("dak'kon %d, morte %d", dak, morte))
		if dak == -1 or morte == -1 then return fail('Dak\'kon and Morte must be in the party') end
		PSTF_EvalAs(dak, 'AddXPObject(Myself,3000000)')  -- real level-ups (mage levels for Cone of Cold etc.)
		PSTF_EvalAs(dak, 'GiveItemCreate("SPICE",Myself,1,0,0)')
		PSTF_EvalAs(morte, 'GiveItemCreate("EMBALM",Myself,1,0,0)')
	end
	if frames == 20 then for _, t in ipairs(thugs) do PSTF_MakeEnemy(t) end end
	local id, r, party, used, acting = PSTF_TbcCurrent()
	if id ~= -1 then started = true end
	if started and id == -1 then
		return fail('fight ended before the plan was done (round ' .. round .. ')')
	end
	if id ~= cur then
		-- the turn that just ended: did the item take the action?
		if cur ~= -1 and itemTurns[cur] and itemTurns[cur].round == round and not itemTurns[cur].used then
			return fail(name(cur) .. ' used ' .. itemTurns[cur].item .. ' but the action was not taken')
		end
		cur, turnStart, round, issued = id, frames, r, -1
		if id == dak then dakTurns = dakTurns + 1 end
		if id == morte then morteTurns = morteTurns + 1 end
		PSTF_Note(string.format('turn of %s round %d: thug hp %d %d %d', name(id), r, hp(thugs[1]), hp(thugs[2]), hp(thugs[3])))
	end
	if id ~= -1 and itemTurns[id] and itemTurns[id].round == round and used then itemTurns[id].used = true end
	-- Adder's Kiss: any combatant (snapshot at the cast: it may die and leave the fight) losing HP in Dak'kon's
	-- Adder turn (the swarm is cast in a later turn)
	if adderRound == round and cur == dak and adderSnap then
		for w, h0 in pairs(adderSnap) do
			local h = hp(w)
			if h < h0 then
				PSTF_Note(string.format("Adder's Kiss: %s %d -> %d, %d frames into the cast turn", name(w), h0, h, frames - turnStart))
				adderHit = true
				adderSnap[w] = h
			end
		end
	end
	-- every thug: HP lost only in Dak'kon's turns, at most once per round each from the swarm
	for _, t in ipairs(thugs) do
		local h = hp(t)
		if seen[t] and h < seen[t] then
			PSTF_Note(string.format('%s: %d -> %d in the turn of %s, round %d, %d frames in', name(t), seen[t], h, name(cur), round, frames - turnStart))
			if cur == dak then
				pulses[t] = pulses[t] or {}
				pulses[t][round] = (pulses[t][round] or 0) + 1
			elseif cur ~= t then
				-- Adder's Kiss' later hits land at the victim's own turn start; anything else is a party melee
				-- swing (party members only end their turns here) or a thug hitting a thug: not expected
				return fail(name(t) .. ' lost HP outside Dak\'kon\'s turn and its own')
			end
		end
		seen[t] = h
	end
	if id == -1 or not party then return end
	local k = frames - turnStart
	if id == dak and issued == -1 and k == 5 then
		local what = plan[dakTurns]
		issued = frames
		if what == 'swarm' then
			PSTF_EvalAs(dak, 'SpellNoDec(NearestEnemyOf(Myself),2212)')
		elseif what == 'adder' then
			adderRound = round
			adderSnap = {}
			local n = 1
			while true do
				local w = PSTF_FightId(n)
				if w == -1 then break end
				n = n + 1
				adderSnap[w] = hp(w)  -- NearestEnemyOf picks any foe, a thug too (the swarm comes after it)
			end
			PSTF_EvalAs(dak, 'SpellNoDec(NearestEnemyOf(Myself),2201)')
		elseif what == 'orb' then
			PSTF_EvalAs(dak, 'SpellNoDec(NearestEnemyOf(Myself),2101)')  -- Chromatic Orb: save vs. spell
		elseif what == 'flask' then
			itemTurns[dak] = {item = 'SPICE', round = round, used = false}
			PSTF_EvalAs(dak, 'UseItem("SPICE",Myself)')
		end
		PSTF_Note('Dak\'kon: ' .. tostring(what) .. ' in round ' .. round)
	end
	if id == morte and issued == -1 and k == 5 and morteTurns == 1 then
		issued = frames
		itemTurns[morte] = {item = 'EMBALM', round = round, used = false}
		PSTF_EvalAs(morte, 'UseItem("EMBALM",Myself)')
		PSTF_Note('Morte: embalming fluid in round ' .. round)
	end
	local waited = issued == -1 and k > 10 or (issued ~= -1 and frames - issued > 150)
	if waited and not acting then PSTF_EndTurn() end
	if dakTurns > #plan + 1 then
		for _, t in ipairs(thugs) do
			for rr, n in pairs(pulses[t] or {}) do
				PSTF_Note(string.format('%s: %d hits in Dak\'kon\'s turn of round %d', name(t), n, rr))
				if n > 2 then return fail(name(t) .. ' took ' .. n .. ' hits in one round (swarm pulses in real time?)') end
			end
		end
		if not adderHit then return fail('Adder\'s Kiss did not hit in the turn it was cast') end
		PSTF_Note('TEST PASS: swarm paced by rounds, Adder\'s Kiss hit at once, items took the action')
		pstfTestTick = nil
	end
end
