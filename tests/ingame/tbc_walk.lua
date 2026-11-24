-- SPDX-License-Identifier: GPL-2.0-or-later
-- Turn-based walking of creatures outside the party (Hive save 000000126, party AI on, three hostiles):
--   Animation  while an enemy moves on its turn it plays its walk sequence (10) and the frames advance: no
--              "gliding" (position moving, sprite standing still);
--   Run        an enemy whose walk path to its target is longer than its movement runs (AI run count).
-- Every moving tick of a creature outside the party is sampled: position, sprite sequence, animation frame.
--   tools\launch.ps1 -Save 000000126 -Lua tests\ingame\tbc_walk.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'tbc_walk: ok|TEST FAIL' -Close -Timeout 300
local frames = 0
local foes = {}
local seen = {}   -- id -> {moving, walkSeq, frameSteps, lastX, lastY, lastFrame, flag}
local turns, lastCur = 0, -1
local jumped = false
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function party(id)
	for i = 0, 7 do if PSTF_PartyId(i) == id then return true end end
	return false
end
local function anim(id)
	local t = {}
	for n in string.gmatch(PSTF_AnimProbe(id), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return t[1], t[2], t[3], t[4]  -- run flag (-1: not a PST character animation), cycle, frame, sprite sequence
end
local function aiRuns()
	local t = {}
	for n in string.gmatch(PSTF_MoveStats(), '(%-?%d+)') do table.insert(t, tonumber(n)) end
	return t[1], t[11]
end
local function report()
	local bad = nil
	for id, s in pairs(seen) do
		PSTF_Note(string.format('tbc_walk: %X moving %d ticks: walk sequence %d, frame steps %d (anim flag %d)', id, s.moving,
			s.walkSeq, s.frameSteps, s.flag or -9))
		if s.moving >= 6 and (s.walkSeq * 2 < s.moving or (s.flag ~= -1 and s.frameSteps * 3 < s.moving)) then
			bad = string.format('%X glides: %d moving ticks, %d in the walk sequence, %d frame steps', id, s.moving, s.walkSeq, s.frameSteps)
		end
	end
	local runs, ai = aiRuns()
	PSTF_Note(string.format('tbc_walk: runs %d, AI maneuvers %d', runs, ai))
	if bad then return fail(bad) end
	if runs < 1 then return fail('no far enemy ran to close the distance') end
	PSTF_Note('tbc_walk: ok')
	pstfTestTick = nil
end
function pstfTestTick()
	frames = frames + 1
	if frames == 2 then
		-- the thugs stand ~20-25 cells off first: their walks are long enough to see (and to need a run)
		local x, y = PSTF_SpritePos(PSTF_PartyId(0))
		local off = {{520, 0}, {360, 60}, {480, -120}}
		for i, id in ipairs({0x180018, 0x140014, 0x1C001C}) do
			PSTF_EvalAs(id, 'JumpToPoint([' .. math.floor(x + off[i][1]) .. '.' .. math.floor(y + off[i][2]) .. '])', 1)
		end
		return
	end
	if frames == 5 then
		for _, id in ipairs({0x180018, 0x140014, 0x1C001C}) do  -- Hive thugs (tbc_hive.lua)
			PSTF_MakeEnemy(id)
			PSTF_SetHp(id, 300)  -- they live long enough to walk up
			table.insert(foes, id)
		end
		PSTF_Note('tbc_walk: ' .. #foes .. ' hostiles')
		if #foes == 0 then return fail('no creature to make hostile') end
		return
	end
	if frames < 5 then return end
	local cur = PSTF_TbcCurrent()
	if cur ~= lastCur then
		lastCur = cur
		if cur ~= -1 and not party(cur) then
			turns = turns + 1
			if cur == 0x180018 and not jumped then
				-- its turn starts ~44 ft (700 px) from the party: a walk cannot close that, it must run
				jumped = true
				local x, y = PSTF_SpritePos(PSTF_PartyId(0))
				PSTF_EvalAs(cur, 'JumpToPoint([' .. math.floor(x + 700) .. '.' .. math.floor(y) .. '])', 1)
				PSTF_EvalAs(cur, 'Attack(Player1)')
			end
		end
	end
	if cur ~= -1 then
		local x, y, _, _, _, path = PSTF_SpritePos(cur, 1)
		local flag, _, frame, seq = anim(cur)
		local s = seen[cur]
		if not s then
			s = {moving = 0, walkSeq = 0, frameSteps = 0, lastX = x, lastY = y, lastFrame = frame, flag = flag}
			seen[cur] = s
		end
		if x ~= s.lastX or y ~= s.lastY then
			s.moving = s.moving + 1
			if seq == 10 then s.walkSeq = s.walkSeq + 1 end
			if frame ~= s.lastFrame then s.frameSteps = s.frameSteps + 1 end
			if s.moving <= 12 then
				local _, cyc = anim(cur)
				PSTF_Note(string.format('tbc_walk: %X at %d,%d seq %d cycle %d frame %d run %d scale %s', cur, x, y, seq, cyc, frame, flag, tostring(PSTF_SmoothTicks and PSTF_SmoothTicks() or '')))
			end
		end
		s.lastX, s.lastY, s.lastFrame, s.flag = x, y, frame, flag
	end
	if turns >= 7 or frames > 3000 then return report() end
end
