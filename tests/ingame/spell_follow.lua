-- SPDX-License-Identifier: GPL-2.0-or-later
-- Follow one spell turn by turn (all-spells save 000000990): a party caster casts it in a fight, then at the end of
-- the cast turn and at every turn start of the creature it changed (the "owner"), log its effects (op:type:ticks),
-- the chosen derived stats, its STATE bits, HP and status icons. A probe for vetting spells by hand, not a pass/fail
-- test: it ends with 'spell_follow: ok' after N owner turn starts.
-- Prefix (tools\spell_follow.ps1 writes it):
--   pstfFollow = {caster = 'Grace', cast = 'SpellNoDec(Myself,1302)', owner = 'self' | 'foe' | 'hit', turns = 4,
--                 stats = {14, 84}, level = 14}
-- owner = a party member's name follows that member. owner 'hit' = the creature whose HP or effects changed in the cast turn (NearestEnemyOf picks any thug).
-- Run: tools\spell_follow.ps1 -Caster Grace -Cast 'SpellNoDec(Myself,1302)' -Owner self -Stats 14,84
local F = pstfFollow or {}
local frames, cur, turnStart, castTurnOver = 0, -1, 0, false
local caster, owner, foe = nil, nil, 0x180018
local castFrame, issued, ownerTurns = nil, false, 0
local snap0, fx0 = {}, {}  -- HP and effect list of every combatant just before the cast (owner 'hit')
local function note(m) PSTF_Note('spell_follow: ' .. m) end
local function fail(m) PSTF_Note('TEST FAIL: spell_follow: ' .. m) pstfTestTick = nil end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
local function isParty(id)
	for k = 0, 7 do if PSTF_PartyId(k) == id then return true end end
	return false
end
local function show(id, when)
	local st = (#(F.stats or {}) > 0) and PSTF_Stat(id, (table.unpack or unpack)(F.stats)) or '-'
	local _, icons = PSTF_Status(id, 0)  -- logged with the raw parent effects
	PSTF_Effects(id)  -- logs op:type:ticks
	local x, y = PSTF_SpritePos(id, 1)
	PSTF_Attached(id)
	if F.lists then for w in pairs(snap0) do PSTF_AreaLists(w) end end
	note(string.format('%s: %X hp %d stats [%s] state %X icons %s at %d,%d', when, id, hp(id), st, PSTF_State(id, 1) or 0,
		(icons ~= '' and icons) or 'none', x or -1, y or -1))
end
local function rig(f)
	local first = 1
	for _, st in ipairs({'SAVEVSDEATH', 'SAVEVSWANDS', 'SAVEVSPOLY', 'SAVEVSBREATH', 'SAVEVSSPELL'}) do
		PSTF_EvalAs(f, 'ChangeStat(Myself,' .. st .. ',' .. (F.saves == 'pass' and -10 or 30) .. ',SET)', first)
		first = 0
	end
	PSTF_EvalAs(f, 'ChangeStat(Myself,RESISTMAGIC,0,SET)', 0)
	if F.foePre and F.foePre ~= '' then PSTF_EvalAs(f, F.foePre, 0) end  -- e.g. THAC0 0: every swing hits
	PSTF_SetHp(f, 1000, 1000)
end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		local s = slotOf(F.caster or 'Grace')
		if s < 0 then return fail('no ' .. tostring(F.caster) .. ' in the party (use save 000000990)') end
		caster = PSTF_PartyId(s)
		-- pre = an action on the caster before the fight (ChangeStat is queued: a turn start clears it)
		if F.pre and F.pre ~= '' then C:Eval(F.pre, s) end
		if F.level then
			if (F.caster or 'Grace') == 'Nameless' then C:Eval('ChangeStat(Myself,LEVEL2,' .. F.level .. ',SET)', s)
			else C:Eval('ChangeStat(Myself,LEVEL,' .. F.level .. ',SET)', s) end
		end
	end
	if frames == 30 then PSTF_MakeEnemy(foe) rig(foe) end
	if frames < 33 then return end
	local id = PSTF_TbcCurrent()
	if id == -1 then
		if frames > 600 then return fail('no fight') end
		return
	end
	if id ~= cur then
		if issued and cur == caster and not castTurnOver then
			castTurnOver = true
			if F.owner == 'hit' then
				owner = nil
				for w, h0 in pairs(snap0) do
					if hp(w) ~= h0 or (fx0[w] and PSTF_Effects(w) ~= fx0[w]) then owner = w end  -- hurt, or new effects
				end
				owner = owner or foe
			end
			show(owner, 'end of the cast turn')
		end
		cur, turnStart = id, frames
	end
	-- the foe attacks a chosen party member on its turns (Black-Barbed Shield's barbs: foeAttacks = 'Player1')
	if castTurnOver and F.foeAttacks and id == foe and frames == turnStart + 2 then
		PSTF_EvalAs(foe, 'Attack(' .. F.foeAttacks .. ')', 1)
	end
	-- the caster attacks the foe on its later turns (Enoll Eva's duplicated swings: casterAttacks = true)
	if castTurnOver and F.casterAttacks and id == caster and frames == turnStart + 2 then
		PSTF_EvalAs(caster, 'Attack(NearestEnemyOf(Myself))', 1)
	end
	-- the owner as seen in other creatures' turns (protections re-applied by an attached object: Antimagic Shell)
	if castTurnOver and F.during and id ~= owner and id ~= -1 and frames == turnStart + 10 then
		show(owner, string.format('during the turn of %X', id))
	end
	if castTurnOver and id == owner and frames == turnStart + 3 then
		ownerTurns = ownerTurns + 1
		show(owner, 'owner turn start ' .. ownerTurns)
		if ownerTurns >= (F.turns or 4) then note('ok') pstfTestTick = nil return end
	end
	-- the caster's first turn: cast; its turn ends when its action is done (the module holds it for the spell)
	if id == caster and not issued and frames == turnStart + 4 then
		issued, castFrame = true, frames
		owner = (F.owner == 'foe' and foe) or caster
		if F.owner ~= 'foe' and F.owner ~= 'self' and F.owner ~= 'hit' and slotOf(F.owner) >= 0 then
			owner = PSTF_PartyId(slotOf(F.owner))  -- a party member by name (cast at it: 'SpellNoDec("Morte",1302)')
		end
		for i = 1, 40 do
			local w = PSTF_FightId(i)
			if w == -1 then break end
			local fh = F.foeHp or 1000
			if not isParty(w) then PSTF_SetHp(w, fh, fh) end
			snap0[w] = (not isParty(w)) and fh or hp(w)
			if not isParty(w) then fx0[w] = PSTF_Effects(w) end
		end
		if F.ownerHp then PSTF_SetHp(owner, F.ownerHp) end  -- room for a heal (Blood Bridge, cures)
		show(owner, 'before the cast')
		note('cast by ' .. string.format('%X', caster) .. ': ' .. F.cast)
		PSTF_EvalAs(caster, F.cast, 1)
	elseif id == caster and issued and not castTurnOver and frames - castFrame > 60 then
		local _, _, _, used, acting = PSTF_TbcCurrent()
		if not acting then PSTF_EndTurn() end
	elseif id ~= -1 and isParty(id) and frames == turnStart + 6 and not (id == caster and not issued) and not (F.casterAttacks and id == caster) then
		PSTF_EndTurn()
	end
	if castTurnOver and F.casterAttacks and id == caster and frames > turnStart + 60 then
		local _, _, _, _, acting = PSTF_TbcCurrent()
		if not acting and frames - turnStart > 300 then PSTF_EndTurn() end
	end
	if frames > 30000 then fail('timeout') end
end
