-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the camera follow with sub-tick interpolation ([smooth] interpolation=1 and
-- [tbc] camera_smooth=1): while a creature walks, the camera must step on the drawn frames that did NOT
-- advance the world too — a tick-rate camera jumps one whole tick step while the mover is drawn gliding,
-- which is the jaggy running seen when the camera is centred on the creature.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth_cam.lua
--      -Env @{PSTF_TBC_AUTOPARTY='1'; PSTF_TEST_TICK='1'} -Until 'smooth_cam: ok|TEST FAIL' -Close -Timeout 240
-- The hostile is healed so the fight outlives the measuring window, and whoever stands still on its turn
-- is ordered to walk to it, so the camera has something to follow; party AI still attacks on each turn.
-- "Walking" is the creature's own path flag (PSTF_SpritePos' 6th value): for some creatures the engine
-- advances the position only every other tick, so position deltas alone cannot prove a continuous walk.
local id = PSTF_MakeEnemyNearest()
if id == -1 then
  PSTF_Note('TEST FAIL: no creature to make hostile')
  return
end
if PSTF_SetHp(id, 200) ~= 1 then
  PSTF_Note('TEST FAIL: could not heal the hostile')
  return
end
local ex, ey = PSTF_SpritePos(id)
local goal = 'MoveToPoint([' .. math.floor(ex) .. '.' .. math.floor(ey + 10) .. '])'
C:Eval(goal, 0)

local seen, walked, windows, fight = 0, 0, {}, false
local pf, pt, pc, pp, pcur, pwalk = 0, 0, 0, 0, -1, false

function fail(msg)
  PSTF_Note('TEST FAIL: ' .. msg)
  pstfTestTick = nil
end

function ok()
  local vx, vy = PSTF_ViewCenter()
  if vx == -1 then
    fail('no camera')
    return
  end
  PSTF_Note('smooth_cam: ok ' .. walked .. ' walking windows, camera steps/ticks/frames ' ..
           table.concat(windows, ' ') .. ', centre ' .. vx .. ',' .. vy)
  pstfTestTick = nil
end

function pstfTestTick()
  seen = seen + 1
  local f, t = PSTF_SmoothFrames(), PSTF_SmoothTicks()
  local c, pan = PSTF_CameraSteps()
  local cur = PSTF_TbcCurrent()
  if cur == -1 then
    pf, pt, pc, pp, pcur, pwalk = f, t, c, pan, -1, false
    if fight and walked >= 3 then
      ok()  -- the fight ended, and enough walking was measured
      return
    end
    if seen > 240 then fail('no turn-based fight with enough walking') end
    return
  end
  fight = true

  local _, _, _, _, _, path = PSTF_SpritePos(cur, 1)
  local walking = path == 1  -- a number: 0 is true in Lua
  local same = cur == pcur
  -- The creature had a path at both ends of this window, on the same creature's turn, with no quick pan:
  -- it walked through every drawn frame of the window, so every one of them must have moved the camera.
  if walking and pwalk and same and pp == 0 and pan == 0 then
    local df, dt, dc = f - pf, t - pt, c - pc
    walked = walked + 1
    if dc <= dt then
      fail('camera stepped ' .. dc .. ' times in ' .. dt .. ' ticks while it walked (' .. df .. ' frames)')
      return
    end
    if dc > df + 1 then
      fail('camera stepped ' .. dc .. ' times over only ' .. df .. ' drawn frames')
      return
    end
    windows[#windows + 1] = dc .. '/' .. dt .. '/' .. df
  elseif same and not walking and seen < 160 then
    PSTF_EvalAs(cur, goal)  -- it only waits: order a walk so the camera has something to follow
  end
  pf, pt, pc, pp, pcur, pwalk = f, t, c, pan, cur, walking

  if seen >= 240 then
    if walked < 3 then fail('only ' .. walked .. ' walking windows') return end
    ok()
  end
end
