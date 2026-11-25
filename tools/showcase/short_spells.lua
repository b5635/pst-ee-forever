-- SPDX-License-Identifier: GPL-2.0-or-later
-- Short clip "spells": a turn-based Hive fight (all-spells save 000000990) where the party casts:
-- the Nameless One Ax of Torment (SPWI310), Fall-from-Grace Spiritual Hammer (SPPR204), the others attack.
-- Party turns are driven here (no AUTOPARTY): an order at turn start, End Turn once the action is spent.
-- Foes are kept alive until both spells landed, then the fight runs 600 more frames.
-- Env PSTF_TEST_TICK=1, PSTF_FEEDBACK_LOG=1 (PSTF_Casts). Starts after ~15 s. Ends with 'short_spells: done'.
local START = 520
local frames, cur, turnFrame = 0, -1, 0
local tno, grace = nil, nil
local plan = {}       -- caster id -> spell number (cast on its first turn)
local casts0 = {}
local landed, endAt = 0, nil
local foes = {}
local function note(s) PSTF_Note('short_spells: ' .. s) end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function slotOf(name)
	local i = 0
	for v in PSTF_PartyNames():gmatch('[^,]+') do
		if v == name then return i end
		i = i + 1
	end
	return -1
end
local function party(id)
	for i = 0, 7 do if PSTF_PartyId(i) == id then return i end end
	return -1
end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end

function pstfTestTick()
	frames = frames + 1
	if frames == 3 then
		tno, grace = PSTF_PartyId(0), PSTF_PartyId(slotOf('Grace'))
		if not grace or grace == -1 then return fail('no Grace: use the all-spells save 000000990') end
		plan[tno], plan[grace] = 2310, 1204
		casts0[tno], casts0[grace] = PSTF_Casts(tno), PSTF_Casts(grace)
		return
	end
	if frames < START then return end
	if frames == START then
		local first = PSTF_MakeEnemyNearest()
		foes = {first}
		for _, k in ipairs({0x180018, 0x140014, 0x1C001C}) do
			local x = PSTF_SpritePos(k, 1)
			if k ~= first and x and x >= 0 and party(k) < 0 then PSTF_MakeEnemy(k) foes[#foes + 1] = k end
		end
		for _, k in ipairs(foes) do PSTF_SetHp(k, 90) end
		local x, y = PSTF_SpritePos(first, 1)
		C:Eval('MoveToPoint([' .. math.floor(x) .. '.' .. math.floor(y + 60) .. '])', 0)
		note(#foes .. ' foes')
		return
	end
	-- foes stand until both spells landed; party members never drop
	if not endAt then for _, k in ipairs(foes) do if hp(k) > 0 and hp(k) < 25 then PSTF_SetHp(k, 60) end end end
	for i = 0, 7 do
		local p = PSTF_PartyId(i)
		if p and p ~= -1 and hp(p) > 0 and hp(p) < 30 then PSTF_SetHp(p, 70) end
	end
	local id, _, _, used, acting = PSTF_TbcCurrent()
	if id ~= cur then cur, turnFrame = id, frames end
	if id ~= -1 and party(id) >= 0 then
		local t = frames - turnFrame
		if t == 3 then
			if plan[id] then
				PSTF_EvalAs(id, 'SpellNoDec(NearestEnemyOf(Myself),' .. plan[id] .. ')', 1)
				note(string.format('%X casts %d', id, plan[id]))
			else
				PSTF_EvalAs(id, 'Attack(NearestEnemyOf(Myself))', 1)
			end
		elseif t > 3 then
			if plan[id] and PSTF_Casts(id) > casts0[id] then
				plan[id] = nil
				landed = landed + 1
				note(string.format('%X cast landed (%d of 2)', id, landed))
				if landed == 2 then endAt = frames + 600 end
			end
			local _, busy, path = PSTF_Action(id, 1)
			if (used and not acting and busy ~= 1 and path ~= 1 and t > 90) or t > 420 then PSTF_EndTurn() end
		end
	end
	if endAt and frames >= endAt then note('done') pstfTestTick = nil end
	if frames > START + 9000 then fail('the spells never landed (' .. landed .. ' of 2)') end
end
