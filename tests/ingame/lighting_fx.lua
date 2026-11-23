-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for lighting phase 4 (glow draws -> emissive, bloom, effect lights) and phase 5 (atmosphere).
-- Hive save: Dak'kon gets XP (real level-ups) and casts Magic Missile (SPWI107) at Morte, as in a game.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting_fx.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_fx: ok|TEST FAIL' -Close -Timeout 200
-- Passes when, while the spell plays: glow draws are gathered, their frames are found in vfxauto.tsv,
-- effect lights are made and one of them stands within 200 px of the caster (the casting glow).
-- Shots: pst_forever/logs/fx_{emissive,lit,off}.bmp at the brightest moment.
local function name(id)
  local _, _, _, _, ref = PSTF_SpritePos(id, 1)
  return ref and Infinity_FetchString(ref) or tostring(id)
end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local dak, morte, morte_slot, n = -1, -1, -1, 0
local best = {glows = 0, hits = 0, lights = 0, near = 1e9, bloom = 0, cov = -1, caps = 0}
local cast_at, shot_step = -1, 0

function pstfTestTick()
  n = n + 1
  if n == 2 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then
        local nm = name(id)
        if nm:find('Dak') then dak = id end
        if nm:find('Morte') then morte, morte_slot = id, s end
      end
    end
    if dak == -1 or morte == -1 then return fail("Dak'kon and Morte must be in the party") end
    PSTF_EvalAs(dak, 'AddXPObject(Myself,3000000)')
    return
  end
  if n == 30 then
    -- Morte is a party member: cast at him by his script name through LastTalkedTo-free targeting.
    local mx, my = PSTF_SpritePos(morte, 1)
    PSTF_EvalAs(dak, 'SpellNoDec(Player' .. (morte_slot + 1) .. ',2107)')
    cast_at = n
    PSTF_Note('lighting_fx: cast SPWI107 by ' .. name(dak) .. ' at ' .. name(PSTF_PartyId(morte_slot)) .. ' (morte at ' .. tostring(mx) .. ',' .. tostring(my) .. ')')
    return
  end
  if cast_at < 0 then return end
  local glows, hits, lights, frames, lx, ly = PSTF_LightFx()
  local dx, dy = PSTF_SpritePos(dak, 1)
  if glows > best.glows then best.glows = glows end
  if hits > best.hits then best.hits = hits end
  if lights > best.lights then best.lights = lights end
  local cov, caps = PSTF_LightCoverage()
  if cov > best.cov then best.cov = cov end
  best.caps = caps
  local bl = PSTF_LightBloom()
  if bl > best.bloom then best.bloom = bl end
  if lights > 0 and dx then
    local d = math.sqrt((lx - dx) ^ 2 + (ly - dy) ^ 2)
    if d < best.near then best.near = d end
  end
  -- shots once the spell glows: emissive mask + bloom, lit, off
  if glows > 0 and shot_step == 0 then PSTF_LightDebug(5) shot_step = 1 shot_at = n end
  if shot_step == 1 and n == shot_at + 2 then PSTF_LightShot('pst_forever/logs/fx_emissive.bmp') PSTF_LightDebug(0) shot_step = 2 end
  if shot_step == 2 and n == shot_at + 4 then PSTF_LightShot('pst_forever/logs/fx_lit.bmp') PSTF_LightDebug(4) shot_step = 3 end
  if shot_step == 3 and n == shot_at + 6 then PSTF_LightShot('pst_forever/logs/fx_off.bmp') PSTF_LightDebug(0) shot_step = 4 end
  if n < cast_at + 150 then return end
  PSTF_Note(string.format('lighting_fx: max glows %d, table hits %d, effect lights %d, nearest light to caster %.0f px, bloom %.3f, captured draws %d, capture/rect %.3f',
    best.glows, best.hits, best.lights, best.near, best.bloom, best.caps, best.cov))
  if best.glows == 0 then return fail('no glow draws while the spell played') end
  if best.hits == 0 then return fail('no glow frame found in vfxauto.tsv (cell resref/sequence/frame)') end
  if best.lights == 0 then return fail('no effect light') end
  if best.near > 200 then return fail('no effect light near the caster') end
  if best.bloom <= 0.01 then return fail('no bloom from the glow draws') end
  if best.caps == 0 then return fail('no emitting draw was captured') end
  if best.cov <= 0 or best.cov >= 0.9 then return fail('emissive capture is not per pixel (capture/rect ' .. best.cov .. ')') end
  PSTF_Note('lighting_fx: ok')
  pstfTestTick = nil
end
