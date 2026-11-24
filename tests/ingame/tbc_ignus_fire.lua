-- SPDX-License-Identifier: GPL-2.0-or-later
-- Ignus' fire that starts a fight (all-spells save 000000990): Ignus attacks a Hive thug out of combat
-- (ForceAttack), and the thug turns hostile when the fire leaves him, so the fight starts with Ignus'
-- projectile flying. It must fly on and land (tbc round_rules.h ProjectilePace), not hang in mid-air until
-- Ignus' own turn comes.
--   tools\launch.ps1 -Save 000000990 -Lua tests\ingame\tbc_ignus_fire.lua -Env @{PSTF_TEST_TICK='1'} -Until 'tbc_ignus_fire: ok|TEST FAIL' -Close -Timeout 300
local frames, foe, ignus, slot = 0, 0x180018, -1, -1
local base, fired, hp0, ignusTurn, started = nil, nil, nil, false, nil
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function hp(id) local _, _, h = PSTF_SpritePos(id, 1) return h or -1 end
function pstfTestTick()
	frames = frames + 1
	if frames == 2 then
		local names = PSTF_PartyNames and PSTF_PartyNames() or ''
		local i = 0
		for nm in string.gmatch(names, '[^;|,]+') do
			if nm:find('Ignus') then slot = i end
			i = i + 1
		end
		if slot < 0 then return fail('Ignus must be in the party (' .. names .. ')') end
		ignus = PSTF_PartyId(slot)
		base = PSTF_Objects(ignus)
		local fx, fy = PSTF_SpritePos(foe, 1)
		PSTF_EvalAs(ignus, string.format('JumpToPoint([%d.%d])', fx - 450, fy + 40), 1)
		return
	end
	if frames == 6 then
		PSTF_EvalAs(foe, string.format('ForceAttack(Player%d,Myself)', slot + 1), 1)
		PSTF_Note('tbc_ignus_fire: Ignus (slot ' .. slot .. ') ordered to attack the thug')
		return
	end
	if frames < 7 then return end
	local n = PSTF_Objects(ignus)
	if not fired then
		if n > base then
			fired, hp0 = frames, hp(foe)
			PSTF_MakeEnemy(foe)  -- the fight starts with the fire in the air
			PSTF_Note(string.format('tbc_ignus_fire: fire in the air (%d objects), the thug turns hostile, hp %d', n - base, hp0))
		elseif frames > 400 then
			return fail('Ignus never fired')
		end
		return
	end
	local id = PSTF_TbcCurrent()
	if id ~= -1 and not started then
		started = frames
		PSTF_Note(string.format('tbc_ignus_fire: the fight started %d frames after the fire, %d objects in the air', frames - fired, n - base))
	end
	if id == ignus and not ignusTurn then
		ignusTurn = true
		if frames - fired < 3 then
			PSTF_Note('tbc_ignus_fire: ok (Ignus acts first: nothing to check)')
			pstfTestTick = nil
			return
		end
		return fail(string.format('the fire was still in the air (%d objects) when Ignus\' turn came, %d frames on', n - base, frames - fired))
	end
	if n <= base then
		if not started then return fail('the fire landed before the fight started: nothing tested') end
		PSTF_Note(string.format('tbc_ignus_fire: the fire landed %d frames after the fight started (turn of %d), thug hp %d -> %d',
			frames - started, id, hp0, hp(foe)))
		PSTF_Note('tbc_ignus_fire: ok')
		pstfTestTick = nil
		return
	end
	-- turns are not ended here: a party turn waits for End, as a player's does, while the fire should land
	if frames - fired > 600 then return fail(string.format('the fire hangs in the air (%d objects) in the turn of %d', n - base, id)) end
end
