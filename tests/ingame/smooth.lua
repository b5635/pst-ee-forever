-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for sub-tick frame interpolation ([smooth] interpolation=1).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\smooth.lua -Env @{PSTF_TEST_TICK='1'}
--      -Until 'smooth: ok|TEST FAIL' -Close -Timeout 200
-- The world must still advance at Torment's own tick rate while the loop iterates faster.
local seen, f0, t0 = 0, 0, 0

function pstfTestTick()
  seen = seen + 1
  if seen == 1 then
    f0, t0 = PSTF_SmoothFrames(), PSTF_SmoothTicks()
  end
  if seen < 30 then return end

  local frames, ticks = PSTF_SmoothFrames(), PSTF_SmoothTicks()
  local tick_ms, fps = PSTF_SmoothTickMs(), PSTF_SmoothFps()
  local df, dt = frames - f0, ticks - t0

  if tick_ms ~= math.floor(1000 / fps) then
    PSTF_Note('TEST FAIL: tick interval ' .. tick_ms .. ' ms, fps ' .. fps)
    pstfTestTick = nil
    return
  end
  if dt == 0 then
    PSTF_Note('TEST FAIL: the world stopped ticking')
    pstfTestTick = nil
    return
  end
  -- Draw-only iterations exist only while the loop is faster than the tick rate.
  if fps * tick_ms < 1000 and df <= dt then
    PSTF_Note('TEST FAIL: no extra frames (frames ' .. df .. ', ticks ' .. dt .. ', fps ' .. fps .. ')')
    pstfTestTick = nil
    return
  end
  PSTF_Note('smooth: ok frames=' .. df .. ' ticks=' .. dt .. ' fps=' .. fps .. ' tick_ms=' .. tick_ms)
  pstfTestTick = nil
end
