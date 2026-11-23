-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the lighting module, phases 0-1 ([lighting] enabled=1).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting.lua -Env @{PSTF_TEST_TICK='1'}
--      -Until 'lighting: ok|TEST FAIL' -Close -Timeout 200
-- In the save's area (script area travel via C:Eval does not move the party in PST:EE, tried
-- LeaveAreaLUA/LeaveAreaLUAPanic/ActionOverride/MoveGlobalsTo: run it once per save instead):
-- 1. Round trip: copy the world and draw it back with lighting off -> 0 pixels differ (phase 0).
-- 2. Lit: the same check must change pixels (ambient + lights applied).
-- 3. Falloff debug: the GPU light buffer equals the CPU mirror (<= 2% of sampled pixels off by > 0.02).
-- 4. A/B shots: pst_forever\logs\light_<AREA>_off.bmp / _lit.bmp / _light.bmp / _falloff.bmp.
local areas = {{nil}}  -- {'AR0202', x, y} entries would travel first (kept for when travel works)
local ai, n, step, wait, area = 1, 0, 1, 0, '?'

local function fail(msg)
  PSTF_Note('TEST FAIL: ' .. msg)
  pstfTestTick = nil
end

local function check(mode, want_zero)
  if wait == 0 then PSTF_LightDebug(mode) end
  wait = wait + 1
  if wait == 4 then PSTF_LightCheck() end
  if wait < 5 then return false end
  local done, diff, total = PSTF_LightCheckResult()
  if done ~= 1 then
    if wait > 60 then fail('check never ran (composite not reached)') end
    return false
  end
  PSTF_Note('lighting: ' .. area .. ' mode ' .. mode .. ' changed ' .. diff .. ' of ' .. total .. ' px')
  if total <= 0 then fail('empty viewport') return false end
  if want_zero and diff ~= 0 then fail('round trip changed ' .. diff .. ' pixels') return false end
  if not want_zero and diff == 0 then fail('lit frame identical to the unlit one') return false end
  wait = 0
  return true
end

local function shot(mode, name)
  if wait == 0 then PSTF_LightDebug(mode) end
  wait = wait + 1
  if wait < 8 then return false end
  local err = PSTF_LightShot('pst_forever\\logs\\light_' .. area .. '_' .. name .. '.bmp')
  if err ~= '' then fail('shot: ' .. err) return false end
  wait = 0
  return true
end

-- GPU light buffer vs the CPU model (lighting_logic DebugFalloffAt), falloff mode.
local function compare()
  if wait == 0 then PSTF_LightDebug(3) end
  wait = wait + 1
  if wait == 4 then PSTF_LightCompare() end
  if wait < 5 then return false end
  local done, cnt, lit, bad = PSTF_LightCompareResult()
  if done ~= 1 then
    if wait > 60 then fail('compare never ran (no light in view?)') end
    return false
  end
  PSTF_Note('lighting: ' .. area .. ' gpu/cpu ' .. cnt .. ' px, ' .. lit .. ' lit, ' .. bad .. ' differ')
  if cnt > 0 and bad > cnt * 0.02 then fail('GPU light model differs from the CPU mirror in ' .. bad .. ' of ' .. cnt .. ' px') return false end
  wait = 0
  return true
end

local steps = {
  function() return check(2, true) end,
  function() return check(0, false) end,
  function() return shot(4, 'off') end,
  function() return shot(0, 'lit') end,
  function() return shot(1, 'light') end,
  function() return shot(3, 'falloff') end,
  compare,
}

function pstfTestTick()
  n = n + 1
  if n < 10 then return end
  if step == 1 then
    -- travel (once) and wait until the composite runs in the wanted area
    local want = areas[ai]
    if want[1] and wait == 0 then
      PSTF_EvalAs(PSTF_PartyId(0), 'LeaveAreaLUAPanic("' .. want[1] .. '","",[' .. want[2] .. '.' .. want[3] .. '],0)', 1)
      wait = 1
    end
    if n % 20 ~= 0 then return end
    local s = PSTF_LightState()
    area = string.match(s, 'area=(%S+)') or '?'
    local ready = string.find(s, 'ready=1', 1, true) ~= nil
    if ready and area ~= '-' and (not want[1] or area == want[1]) then
      wait, n, step = 0, 0, 2
      PSTF_Note('lighting: in ' .. s)
    elseif n > 400 then
      fail('area ' .. tostring(want[1]) .. ' never drawn: ' .. s)
    end
    return
  end
  if n < 40 then return end  -- let the camera settle after a travel
  if steps[step - 1]() then step = step + 1 end
  if step - 1 > #steps then
    PSTF_LightDebug(0)
    ai, step, wait, n = ai + 1, 1, 0, 0
    if ai > #areas then
      PSTF_Note('lighting: ok ' .. PSTF_LightState())
      pstfTestTick = nil
    end
  end
end
