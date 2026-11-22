-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test: a walk under the engine's own lock camera with [smooth] interpolation=1 and
-- lock_camera=1. The party leader is scroll-locked (the HUD's lock button), ordered to walk, and one
-- row per loop iteration is traced ("smooth trace: ..." in the log; tools/smooth_trace.py summarises it).
-- Pass: while it walks, the shown position never stands still for as long as a tick (the engine lands a
-- step only every other tick: sliding over one tick then held it for a whole tick; at a high refresh
-- rate shorter holds are only sub-pixel motion), the creature stays put on screen
-- against the locked view (wobble <= 1 px; the view followed the tick position: 5 px), and the lock
-- follow ran on the draw-only frames too.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth_lock.lua -Env @{PSTF_TEST_TICK='1'}
--      -Until 'smooth_lock: ok|TEST FAIL' -Close -Timeout 120
local id = PSTF_PartyId(0)
if id == -1 then
  PSTF_Note('TEST FAIL: no party leader')
  return
end
local x, y = PSTF_SpritePos(id)
PSTF_LockCamera(id)
PSTF_EvalAs(id, 'MoveToPoint([' .. math.floor(x + 360) .. '.' .. math.floor(y + 120) .. '])', 1)

local seen, l0, t0 = 0, 0, 0
function pstfTestTick()
  seen = seen + 1
  if seen == 8 then
    PSTF_SmoothTrace(id, 100)  -- walking by now; 100 frames stay inside the walk
    l0, t0 = PSTF_SmoothLockFrames(), PSTF_SmoothTicks()
  end
  if seen < 9 then return end
  local done, frames, walk, still, wobble, hold, tick_ms = PSTF_SmoothTraceResult()
  if done == 0 then
    if seen > 200 then
      PSTF_Note('TEST FAIL: trace never completed')
      pstfTestTick = nil
    end
    return
  end
  local dl, dt = PSTF_SmoothLockFrames() - l0, PSTF_SmoothTicks() - t0
  PSTF_LockCamera(-1)
  pstfTestTick = nil
  if walk < 40 then
    PSTF_Note('TEST FAIL: only ' .. walk .. ' walk frames of ' .. frames)
  elseif hold >= 0.75 * tick_ms then
    PSTF_Note('TEST FAIL: the walker stood still for ' .. hold .. ' ms (tick ' .. tick_ms .. ' ms; ' .. still ..
              ' of ' .. walk .. ' frames without a pixel of motion)')
  elseif wobble > 1 then
    PSTF_Note('TEST FAIL: the walker wobbled ' .. wobble .. ' px against the locked camera')
  elseif dl <= dt then
    PSTF_Note('TEST FAIL: the lock follow ran ' .. dl .. ' times in ' .. dt .. ' ticks (draw-only frames missed)')
  else
    PSTF_Note('smooth_lock: ok walk ' .. walk .. '/' .. frames .. ' frames, longest hold ' .. hold .. ' ms (tick ' ..
              math.floor(tick_ms) .. '), still ' .. still .. ', wobble ' ..
              wobble .. ' px, lock follow ' .. dl .. ' frames / ' .. dt .. ' ticks')
  end
end
