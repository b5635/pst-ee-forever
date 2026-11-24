-- SPDX-License-Identifier: GPL-2.0-or-later
-- 8-member party save/load round trip: loads the quick save party8.lua wrote with 8 members and checks
-- the engine restored all 8 in the same order with a portrait slot each, and the triggers see them.
-- Run after party8.lua: tools\launch.ps1 -Save 000000001 -Lua tests\ingame\party8_load.lua -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'party8_load: ok|TEST FAIL' -Close
local want = 'Nameless,Dakkon,Annah,Morte,Ignus,Vhail,Nordom,Grace'
local f = 0
local function fail(msg) PSTF_Note('TEST FAIL: party8_load: ' .. msg) pstfTestTick = nil end
function pstfTestTick()
	f = f + 1
	if f ~= 3 then return end
	local names = PSTF_PartyNames()
	if names ~= want then return fail('party ' .. names .. ', want ' .. want) end
	local s = PSTF_Party()
	local count = tonumber(s:match('count=(%-?%d+)'))
	if count ~= 8 then return fail('count ' .. tostring(count)) end
	local fixed = s:match('fixed=([^ ]+)')
	if fixed:find('-1', 1, true) then return fail('a portrait slot is empty: ' .. fixed) end
	local tno = PSTF_PartyId(0)
	if PSTF_Trigger(tno, 0x406a, 8, '') ~= 1 then return fail('NumInParty(8)') end
	if PSTF_Trigger(tno, 0x4090, 7, 'Grace') ~= 1 then return fail('InPartySlot("Grace",7)') end
	PSTF_Note('party8_load: ok ' .. names)
	pstfTestTick = nil
end
