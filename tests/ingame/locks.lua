-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the lock rolls (src/modules/locks) on the shared roll panel (src/modules/rolls).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\locks.lua
--      -Env @{PSTF_DLG_TEST='1'; PSTF_TBC_NOFIGHT='1'} -Until 'locks: ok|TEST FAIL' -Close -Timeout 240
-- (PSTF_DLG_TEST runs pstfDlgTest every main-loop iteration, also while the panel pauses the world.)
-- A container near the Nameless One (else a door: the Hive has doors only) is locked at difficulty 20
-- (PSTF_SetLock), then:
-- 1. PickLock: he walks up, the panel opens with DC 14 and modifier lockpicking / 5 + luck, the game is
--    paused; a forced natural 1 fails; the engine keeps it locked; the game runs again.
-- 2. PickLock again at once: the panel only says the lock was failed (no Roll); Back ends the action.
-- 3. BashDoor: DC 17, modifier (STR - 10) / 2 + luck; a forced natural 20 opens it (the engine unlocks it).
-- 4. Locked again at 100 (key only): PickLock gets no panel and the lock stays shut (vanilla).
-- Shots: pst_forever/logs/lock_{pick,pick_fail,blocked,bash,bash_ok}.bmp
local function note(s) PSTF_Note(s) end
local function fail(msg) note('TEST FAIL: ' .. msg) pstfDlgTest = nil end
local function shot(name) PSTF_LightShot('pst_forever/logs/lock_' .. name .. '.bmp') end
local function now() return Infinity_GetClockTicks() end

local phase, t, tno = 'start', now(), -1
local box = nil  -- { id, name, x, y }
local shots = {}
local function go(p) phase, t, shots = p, now(), {} end

local function lockInfo()
	for id, kind, locked, diff, x, y, name in PSTF_Locks(tno, 4000):gmatch('(%-?%d+):(%a+):(%d):(%d+):(%-?%d+),(%-?%d+):([^;]*);') do
		if tonumber(id) == box.id then return tonumber(locked) == 1, tonumber(diff) end
	end
end
local function st(key) return PSTF_LockState():match(key .. '=(%S+)') end
local function paused() return worldScreen:CheckIfPaused() == true end
local function order(action) PSTF_EvalAs(tno, action .. '("' .. box.name .. '")', 1) end

-- the panel cycle shared by pick and bash: open -> check -> roll forced `nat` -> Continue
local function panelPhase(ms, name, nat, want_dc, want_mod, want_out, nextp)
	if not pstfDr.open then
		if ms > 15000 then return fail(name .. ': the roll panel did not open (' .. PSTF_LockState() .. ')') end
		return
	end
	if not shots.open then
		shots.open = true
		if not paused() then return fail(name .. ': the game is not paused under the panel') end
		local dc, mod = tonumber(st('dc')), tonumber(st('mod'))
		note(string.format('locks: %s panel: %s, DC %d, modifier %d', name, pstfDr.r.title, dc, mod))
		if dc ~= want_dc or mod ~= want_mod then
			return fail(string.format('%s: DC %d modifier %d, want %d %d', name, dc, mod, want_dc, want_mod))
		end
		if pstfDr.r.dc ~= dc or pstfDr.r.mod ~= mod then return fail(name .. ': the panel shows other numbers') end
		t = now()
		return
	end
	if not pstfDr.t0 then
		if ms < 700 then return end
		shot(name)
		PSTF_RollForce(nat)
		pstfDrRollClick()
		if not pstfDr.t0 then return fail(name .. ': roll did not start') end
		note(string.format('locks: %s rolled natural %d total %d outcome %d', name, pstfDr.nat, pstfDr.total, pstfDr.out))
		if pstfDr.out ~= want_out then return fail(name .. ': outcome ' .. pstfDr.out .. ', want ' .. want_out) end
		return
	end
	if pstfDrContinueOn() then
		shot(name .. (want_out == 3 and '_ok' or '_fail'))
		pstfDrContinue()
		go(nextp)
	elseif pstfDrMs() > 12000 then
		return fail(name .. ': no Continue after 12 s')
	end
end

function pstfDlgTest()
	local ms = now() - t
	if phase == 'start' then
		if ms < 1500 then return end
		tno = PSTF_PartyId(0)
		if tno == -1 then return fail('no Nameless One') end
		local best
		for id, kind, locked, diff, x, y, name in PSTF_Locks(tno, 4000):gmatch('(%-?%d+):(%a+):(%d):(%d+):(%-?%d+),(%-?%d+):([^;]*);') do
			-- a container if there is one, else a door (the Hive's outdoor area has doors only)
			if name ~= '' and (not best or (kind == 'box' and best.kind == 'door')) then
				best = { id = tonumber(id), name = name, x = tonumber(x), y = tonumber(y), kind = kind }
			end
		end
		if not best then return fail('no named door or container near the Nameless One') end
		box = best
		note(string.format('locks: %s %d "%s" at %d,%d', box.kind, box.id, box.name, box.x, box.y))
		if not PSTF_SetLock(box.id, 1, 20) then return fail('PSTF_SetLock failed') end
		PSTF_LockForget()
		local locked, diff = lockInfo()
		if not locked or diff ~= 20 then return fail('the lock did not take') end
		-- stand him a few steps from it: he still walks up (the approach), but not across the map
		PSTF_EvalAs(tno, string.format('JumpToPoint([%d.%d])', box.x - 90, box.y + 40), 1)
		go('jumped')
	elseif phase == 'jumped' then
		if ms < 800 then return end
		order('PickLock')
		go('pick')
	elseif phase == 'pick' then
		-- the modifier is the module's (its log line has the stats; the formula is unit-tested)
		if not box.mod_pick then
			if st('state') == 'none' then
				if ms > 30000 then return fail('no lock attempt registered') end
				return
			end
			box.mod_pick = tonumber(st('mod'))
		end
		panelPhase(ms, 'pick', 1, 14, box.mod_pick, 2, 'pick_after')
	elseif phase == 'pick_after' then
		if ms < 1500 then return end
		local locked = lockInfo()
		if not locked then return fail('a failed pick opened the lock') end
		if paused() then return fail('the game stayed paused after the panel') end
		if st('state') ~= 'none' then return fail('attempt still pending: ' .. PSTF_LockState()) end
		order('PickLock')
		go('blocked')
	elseif phase == 'blocked' then
		if not pstfDr.open then
			if ms > 15000 then return fail('no panel for the blocked retry') end
			return
		end
		if ms < 700 then return end
		if tonumber(st('blocked')) ~= 1 then return fail('retry not blocked: ' .. PSTF_LockState()) end
		if pstfDrCanRoll() then return fail('the blocked panel offers a roll') end
		if not tostring(pstfDr.r.note):find('failed this lock', 1, true) then return fail('note: ' .. tostring(pstfDr.r.note)) end
		shot('blocked')
		pstfDrBack()
		go('blocked_after')
	elseif phase == 'blocked_after' then
		if ms < 1500 then return end
		if st('state') ~= 'none' then return fail('the blocked attempt did not end: ' .. PSTF_LockState()) end
		if not lockInfo() then return fail('the lock opened on Back') end
		order('BashDoor')
		go('bash')
	elseif phase == 'bash' then
		if not box.mod_bash then
			if st('state') == 'none' then
				if ms > 30000 then return fail('no bash attempt registered') end
				return
			end
			box.mod_bash = tonumber(st('mod'))
		end
		panelPhase(ms, 'bash', 20, 17, box.mod_bash, 3, 'bash_after')
	elseif phase == 'bash_after' then
		local locked = lockInfo()
		if locked then
			if ms > 4000 then return fail('a won bash left the lock shut') end
			return
		end
		note('locks: bash opened the ' .. box.kind)
		go('relock')
	elseif phase == 'relock' then
		-- the engine's unlock also travels as a message (CMessageDoorStatus): let it land before re-locking
		if ms < 1500 then return end
		PSTF_SetLock(box.id, 1, 100)
		local locked, diff = lockInfo()
		if not locked or diff ~= 100 then return fail('could not re-lock at 100') end
		order('PickLock')
		go('key')
	elseif phase == 'key' then
		if pstfDr.open then return fail('a key-only lock got the panel') end
		if ms < 6000 then return end
		if not lockInfo() then return fail('a key-only lock opened') end
		if st('state') ~= 'none' then return fail('key-only lock left an attempt: ' .. PSTF_LockState()) end
		note('locks: ok')
		pstfDlgTest = nil
	end
end
