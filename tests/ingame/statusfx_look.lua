-- SPDX-License-Identifier: GPL-2.0-or-later
-- Look at the statusfx effects: each plays on the Nameless One (camera centred on him), shots at a few
-- moments. Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\statusfx_look.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'statusfx_look: ok|TEST FAIL' -Close -Timeout 200
-- Shots: pst_forever/logs/sfxlook_<fx>_<k>.bmp (python -I tools/statusfx_sheet.py makes a contact sheet).
local plan = {  -- effect, shot ticks after the start, ticks it runs
  {'fear', {15, 35, 60, 90}, 100},
  {'stun', {8, 20, 35, 55}, 70},
  {'stunloop', {15, 30, 45, 60}, 70},
  {'blind', {8, 18, 30, 45}, 70},
  {'acid', {8, 18, 25, 36}, 60},
  {'sonic', {8, 18, 25, 36}, 60},
  {'mantle', {5, 10, 16, 24}, 40},
}
local n, step, t0, tno = 0, 1, 0, -1
function pstfTestTick()
  n = n + 1
  if n == 3 then
    tno = PSTF_PartyId(0)
    PSTF_CenterOn(tno)
    t0 = n + 20
    return
  end
  if tno == -1 or n < t0 then return end
  local p = plan[step]
  if not p then PSTF_Note('statusfx_look: ok') pstfTestTick = nil return end
  local t = n - t0
  if t == 0 then PSTF_CenterOn(tno) PSTF_StatusFxPlay(tno, p[1]) end
  for k, s in ipairs(p[2]) do
    if t == s then PSTF_LightShot('pst_forever/logs/sfxlook_' .. p[1] .. '_' .. k .. '.bmp') end
  end
  if t == p[3] then
    PSTF_StatusFxPlay(tno, 'stop:' .. p[1])
    step = step + 1
    t0 = n + 30
  end
end
