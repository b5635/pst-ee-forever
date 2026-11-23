-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game probe/test: the engine's time-of-day tint and our lights at night.
-- Run: tools\launch.ps1 -Save 000000166 -Lua tests\ingame\lighting_night.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_night: ok|TEST FAIL' -Close -Timeout 200
-- Shots: pst_forever/logs/night_lit.bmp, night_off.bmp, night_light.bmp (light buffer).
local n = 0
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
function pstfTestTick()
  n = n + 1
  if n == 20 then PSTF_LightGlobal() PSTF_LightState() end
  if n == 24 then PSTF_LightShot('pst_forever/logs/night_lit.bmp') PSTF_LightDebug(4) end
  if n == 28 then PSTF_LightShot('pst_forever/logs/night_off.bmp') PSTF_LightDebug(1) end
  if n == 32 then PSTF_LightShot('pst_forever/logs/night_light.bmp') PSTF_LightDebug(0) end
  if n == 36 then
    local tod, applied, dn = PSTF_LightGlobal()
    PSTF_Note(string.format('lighting_night: tod %06X applied %06X daynight %d', tod, applied, dn))
    PSTF_Note('lighting_night: ok')
    pstfTestTick = nil
  end
end
