-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the dialogue ability rolls (src/modules/dlgroll).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\dlgroll.lua
--      -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'dlgroll: ok|TEST FAIL' -Close -Timeout 200
-- A party member opens the Mortuary skeleton's dialogue (DSKELW) with the Nameless One. With the
-- examine/quip globals set, "Try and pry out the skeleton's joint bolts." has a STR <= 12 version and a
-- CheckStatGT(Protagonist,12,STR) version: the second is hidden by the engine and must come back obscured.
-- 1. failure (forced natural 2): the row turns failed and cannot be clicked; leave the dialogue.
-- 2. the dialogue again: the row is rollable again (per node); a forced natural 15 wins (DC 10): the
--    engine numbers the STR > 12 reply, the STR <= 12 one goes away, the Nameless One's STR is untouched,
--    and after picking it the next node has no ability override left.
-- Shots: pst_forever/logs/dlg_{list,hover,panel,tumble,natural,bonus,pop,banner_fail,banner_ok,won}.bmp
local function note(s) PSTF_Note(s) end
local function fail(msg) note('TEST FAIL: ' .. msg) pstfDlgTest = nil end
local function shot(name) PSTF_LightShot('pst_forever/logs/dlg_' .. name .. '.bmp') end
local function now() return Infinity_GetClockTicks() end

local phase, t, speaker, tno = 'start', now(), -1, -1
local rollRow, rollMarker, pass = nil, nil, 1
local shots = {}

local function stat(name)
	local s = PSTF_DlgState()
	return tonumber(s:match(name .. '(%-?%d+),'))
end
local function findRoll()
	for k, v in ipairs(dialogTable) do
		if v.pstfRoll and pstfDlg.rows[v.marker] and pstfDlg.rows[v.marker].ids == 'STR' then return k, v.marker end
	end
end
local function startTalk()
	PSTF_EvalAs(tno, 'DestroyPartyItem("PRYBAR",1)')  -- the prybar route would open the bolts without STR
	PSTF_EvalAs(tno, 'SetGlobal("Skeleton_Examine","GLOBAL",1)')
	PSTF_EvalAs(tno, 'SetGlobal("Morte_Skel_Mort_Quip2","GLOBAL",1)')
	PSTF_EvalAs(speaker, 'StartDialogOverride("DSKELW",Protagonist)', 1)
end
local function go(p) phase, t = p, now() end

function pstfDlgTest()
	local ms = now() - t
	if phase == 'start' then
		if ms < 1500 then return end
		tno = PSTF_PartyId(0)
		for s = 1, 7 do
			local id = PSTF_PartyId(s)
			if id ~= -1 then speaker = id end
		end
		if tno == -1 or speaker == -1 then return fail('need the Nameless One and one more party member') end
		local str = stat('STR')
		note('dlgroll: Nameless One STR ' .. tostring(str))
		if not str or str > 12 then return fail('the Nameless One needs STR 12 or less for this test (has ' .. tostring(str) .. ')') end
		startTalk()
		go('wait_node')
	elseif phase == 'wait_node' then
		if PSTF_DlgNode() == 0 or pstfDlg.node == (pstfDlg.lastNode or -1) then
			if ms > 6000 then return fail('the dialogue did not open') end
			return
		end
		local k, m = findRoll()
		if k then
			rollRow, rollMarker = k, m
			pstfDlg.lastNode = pstfDlg.node
			local r = pstfDlg.rows[m]
			note(string.format('dlgroll: roll row %d (reply %d): %s needs %d, DC %d, modifier %d, text %s', k, m, r.ids, r.need, r.dc, r.mod, getDialogEntryText(k)))
			if r.need ~= 13 or r.dc ~= 10 then return fail('expected STR 13 needed and DC 10') end
			if r.mod ~= math.floor((stat('STR') - 10) / 2) then return fail('modifier ' .. r.mod) end
			local txt = getDialogEntryText(k)
			if not txt:find('[Strength]', 1, true) then return fail('row text has no [Strength] tag') end
			if txt:find('pry', 1, true) then return fail('row text not obscured') end
			go('list')
			return
		end
		if ms > 6000 then
			PSTF_DlgState()
			return fail('no STR roll row in the dialogue (dialogue up: ' .. tostring(hasDialogResponse) .. ')')
		end
	elseif phase == 'list' then
		if ms == 0 then return end
		if not shots.list then shot('list_' .. pass) shots.list = true end
		if ms < 400 then return end
		worldPlayerDialogSelection = rollRow  -- hover: brighter row + glowing die
		local mx, my, mw, mh = Infinity_GetMenuArea('WORLD_DIALOG')
		pstfDlgFakeMouse = { mx + 300, my + 150 }
		if ms < 900 then return end
		if pass == 1 then shot('hover') end
		pstfDlgFakeMouse = nil
		pstfDlgReplyClick(rollMarker)
		if not pstfDr.open then return fail('the roll panel did not open') end
		go('panel')
	elseif phase == 'panel' then
		if ms < 700 then return end
		shot('panel_' .. pass)
		PSTF_RollForce(pass == 1 and 2 or 15)
		pstfDrRollClick()
		if not pstfDr.t0 then return fail('roll did not start') end
		note(string.format('dlgroll: rolled natural %d total %d outcome %d', pstfDr.nat, pstfDr.total, pstfDr.out))
		if pass == 1 and pstfDr.out ~= 0 then return fail('natural 2 should fail') end
		if pass == 2 and pstfDr.out ~= 1 then return fail('natural 15 should succeed') end
		shots = {}
		go('anim')
	elseif phase == 'anim' then
		local a = pstfDrMs()
		local want = { {900, 'tumble'}, {2500, 'natural'}, {3700, 'bonus'}, {pstfDrPopAt() + 120, 'pop'},
			{pstfDrBannerAt() + 500, pass == 1 and 'banner_fail' or 'banner_ok'} }
		for _, w in ipairs(want) do
			if a >= w[1] and not shots[w[2]] then shots[w[2]] = true shot(w[2] .. (pass == 1 and '' or '_ok')) end
		end
		if pstfDrContinueOn() and shots[pass == 1 and 'banner_fail' or 'banner_ok'] then
			pstfDrContinue()
			go('after')
		end
		if a > 12000 then return fail('no Continue after 12 s') end
	elseif phase == 'after' then
		if ms < 500 then return end
		local s = PSTF_DlgState()
		if pass == 1 then
			local r = pstfDlg.rows[rollMarker]
			if not r or r.state ~= 'failed' then return fail('row not marked failed') end
			if getDialogRowClickable(rollRow) then return fail('failed row still clickable') end
			if not s:find(rollMarker .. ':STR:13:10:', 1, true) or not s:find('failed') then return fail('state: ' .. s) end
			shot('failed')
			-- leave: the "Leave the skeleton in peace." rows (14 to Morte, 15/16 end)
			go('leave')
			return
		end
		if not s:find('wins=STR=13') then return fail('no STR 13 override after the win: ' .. s) end
		if stat('STR') > 12 then return fail('the Nameless One\'s STR changed: ' .. s) end
		local won, low = false, false
		for k, v in ipairs(dialogTable) do
			if v.marker == rollMarker and not v.pstfRoll and v.text:find('[Strength]', 1, true) then won = true end
			if v.marker == rollMarker - 1 and v.text:find('pry', 1, true) then low = true end
		end
		if not won then return fail('the won reply is not a normal row') end
		if low then return fail('the STR <= 12 reply is still listed') end
		local num = s:match('[=,]' .. rollMarker .. ':(%-?%d+),')
		if not num or tonumber(num) < 1 then return fail('the engine has no number on the won reply: ' .. s) end
		shot('won')
		go('pick')
	elseif phase == 'leave' then
		if PSTF_DlgNode() == 0 then
			if ms < 1000 then return end
			pass = 2
			startTalk()
			go('wait_node')
			return
		end
		if ms < 900 then return end
		-- the last listed reply leaves (Leave the skeleton in peace / Morte's farewell lines)
		local last
		for k, v in ipairs(dialogTable) do if v.marker ~= nil and not v.pstfRoll then last = v.marker end end
		-- first the reply, then the End Dialogue button for the closing line
		local tries = pstfDlg.leaveTries or 0
		if tries < 3 then
			shot('leave_' .. tries)
			note('dlgroll: leave try ' .. tries .. ' last reply ' .. tostring(last) .. ' node ' .. PSTF_DlgNode() ..
				' response ' .. tostring(hasDialogResponse) .. ' button ' .. tostring(getDialogButtonText()))
		end
		if last and tries == 0 then pstfDlgReplyClick(last) else worldScreen:StepDialog() end
		t = now()
		if (pstfDlg.leaveTries or 0) > 20 then return fail('could not leave the dialogue') end
		pstfDlg.leaveTries = (pstfDlg.leaveTries or 0) + 1
	elseif phase == 'pick' then
		local node = pstfDlg.node
		pstfDlgReplyClick(rollMarker)
		go('next')
		t = now()
		phase = 'next'
		pstfDlg.pickedFrom = node
	elseif phase == 'next' then
		if ms < 1500 then return end
		local s = PSTF_DlgState()
		if pstfDlg.node == pstfDlg.pickedFrom then return fail('the dialogue did not move on after picking the won reply') end
		if not s:find('wins= ') then return fail('the next node kept the override: ' .. s) end
		shot('next')
		note('dlgroll: ok')
		pstfDlgTest = nil
	end
end
