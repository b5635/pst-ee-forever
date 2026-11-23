-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test: BAM blob shadows hidden (bam_shadows=0), our contact shadows drawn, Ignus' creature light
-- fixed at his feet and steady (vfxlight.tsv anim:36).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting_shadows.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_shadows: ok|TEST FAIL' -Close -Timeout 200
-- Shots: pst_forever/logs/shadows_lit.bmp, shadows_sprites.bmp (debug 6: blob pixels would be green).
local function name(id)
  local _, _, _, _, ref = PSTF_SpritePos(id, 1)
  return ref and Infinity_FetchString(ref) or tostring(id)
end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, ignus = 0, -1
local seen, moved, flick, r0 = 0, 0, 0, -1
local walk_ticks, light_holds, engine_holds, last_ly, last_ey = 0, 0, 0, nil, nil

function pstfTestTick()
  n = n + 1
  if n == 2 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 and name(id):find('Ignus') then ignus = id end
    end
    if ignus == -1 then return fail('Ignus must be in the party') end
    return
  end
  if n < 20 then return end
  if n <= 60 then
    local contact, hidden, buf, lx, ly, lr = PSTF_LightShadows()
    if hidden ~= 1 then return fail('BAM shadows not hidden') end
    if contact == 0 or buf ~= 1 then return fail('no contact shadows drawn') end
    local sx, sy = PSTF_SpritePos(ignus, 1)
    if lx >= 0 then
      seen = seen + 1
      if math.abs(lx - sx) > 0.5 or math.abs(ly - sy) > 0.5 then moved = moved + 1 end
      if r0 < 0 then r0 = lr elseif math.abs(lr - r0) > 1e-4 then flick = flick + 1 end
    end
  end
  if n == 30 then PSTF_LightShot('pst_forever/logs/shadows_lit.bmp') PSTF_LightDebug(6) end
  if n == 34 then
    PSTF_LightShot('pst_forever/logs/shadows_sprites.bmp')
    PSTF_LightDebug(0)
    PSTF_LightSpriteCheck()
  end
  if n == 40 then
    local spr, cov, both = PSTF_LightSpriteCheck()
    PSTF_Note(string.format('lighting_shadows: sprite px %d coverage px %d both %d (coverage only %d)', spr, cov,
      both, cov - both))
  end
  if n == 62 then
    PSTF_Note(string.format('lighting_shadows: Ignus light frames %d, off his feet %d, intensity changes %d',
      seen, moved, flick))
    if seen < 30 then return fail('no creature light for Ignus') end
    if moved > 0 then return fail('Ignus light not at his feet') end
    if flick > 0 then return fail('Ignus light intensity changes (should be steady)') end
    -- walk south: the light follows his drawn (smoothed) feet; the engine position holds every other tick
    local sx, sy = PSTF_SpritePos(ignus, 1)
    PSTF_EvalAs(ignus, string.format('MoveToPoint([%d.%d])', sx, sy + 200), 1)
    PSTF_LightCreatureTrace(400)  -- one sample per drawn frame (draw-only frames included)
  end
  if n > 64 and n <= 124 then
    local _, ey, _, _, _, walking = PSTF_SpritePos(ignus, 1)
    if walking == 1 then
      walk_ticks = walk_ticks + 1
      if last_ey and ey == last_ey then engine_holds = engine_holds + 1 end
    end
    last_ey = ey
  end
  if n == 126 then
    local samples, moving, holds, step, astep = PSTF_LightCreatureTrace()
    PSTF_Note(string.format('lighting_shadows: walk ticks %d (engine position holds %d); light frames %d, moving %d, ' ..
      'holds %d, largest step %.2f px (sprite anchor %.2f)', walk_ticks, engine_holds, samples, moving, holds, step,
      astep))
    if walk_ticks < 20 or moving < 20 then return fail('Ignus did not walk') end
    -- smooth: the shown position moves at most ~1-2 px per drawn frame (unsmoothed: 5 px every other tick)
    if step > 2.5 then return fail('Ignus light stutters while he walks') end
    PSTF_Note('lighting_shadows: ok')
    pstfTestTick = nil
  end
end
