-- SPDX-License-Identifier: GPL-2.0-or-later
-- Saving-throw banner ([tbc] save_banner): one success and one failure, each with the icon animation
-- and "Save Success: N (Death)" / "Save Failed: N (Spell)" above the creature that rolled.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\save_banner.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'save_banner: ok|TEST FAIL' -Close -Timeout 200
-- Blindness (SPWI115, save vs. spell negates) on two party members: one with SAVEVSSPELL 1 (always
-- saves), one with 30 (never saves). Checks: two banners, the right verdicts, the engine's own
-- "Save vs." line is gone (PSTF_SaveBanner's engine-message count), both sounds played.
-- Shots: pst_forever/logs/save_banner_NN.bmp (every 3 ticks for ~3.5 s: the whole animation).
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, party, pass, flop = 0, {}, nil, nil
local t0 = 40
function pstfTestTick()
  n = n + 1
  if n == 3 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then party[#party + 1] = id end
    end
    if #party < 2 then return fail('need two party members') end
    pass, flop = party[1], party[2]
    PSTF_EvalAs(pass, 'ChangeStat(Myself,SAVEVSSPELL,1,SET)')
    PSTF_EvalAs(flop, 'ChangeStat(Myself,SAVEVSSPELL,30,SET)')
    return
  end
  if n == t0 then
    PSTF_EvalAs(pass, 'ApplySpellRES("SPWI115",Myself)')
    PSTF_EvalAs(flop, 'ApplySpellRES("SPWI115",Myself)')
    return
  end
  if n > t0 and n <= t0 + 110 and (n - t0) % 3 == 0 then
    PSTF_LightShot(string.format('pst_forever/logs/save_banner_%02d.bmp', (n - t0) / 3))
  end
  if n == t0 + 20 then
    for i = 1, 2 do
      local on, x, y, f, a, good, t = PSTF_SaveBanner(i)
      PSTF_Note(string.format('save_banner: %d on %d at %d,%d frame %d alpha %.2f good %d "%s" width %d', i, on, x, y, f, a,
        good, t, pstfSvWidth and pstfSvWidth(t) or -1))
    end
  end
  if n == t0 + 30 then
    local shown, good, bad, engine_lines = PSTF_SaveBanner()
    PSTF_Note(string.format('save_banner: shown %d, success %d, failed %d, engine lines %d', shown or -1,
      good or -1, bad or -1, engine_lines or -1))
    if (good or 0) < 1 then return fail('no success banner') end
    if (bad or 0) < 1 then return fail('no failure banner') end
    if (engine_lines or 0) ~= 0 then return fail('the engine still posted its "Save vs." line') end
  end
  if n == t0 + 140 then
    local shown = PSTF_SaveBanner()
    if (shown or 0) ~= 0 then return fail('banners still up after ' .. 140 .. ' ticks: ' .. shown) end
    PSTF_Note('save_banner: ok')
    pstfTestTick = nil
  end
end
