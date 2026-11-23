-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test: hand lights (Ignus always burns) and buff colour lights (Improved Strength's tint).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting_lights.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_lights: ok|TEST FAIL' -Close -Timeout 200
-- Shot: pst_forever/logs/lights_ignus.bmp (lit) and lights_ignus_off.bmp.
local function name(id)
  local _, _, _, _, ref = PSTF_SpritePos(id, 1)
  return ref and Infinity_FetchString(ref) or tostring(id)
end
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, dak, ignus, cast_at = 0, -1, -1, -1
local hand_max, buff_max = 0, 0

function pstfTestTick()
  n = n + 1
  if n == 2 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then
        local nm = name(id)
        if nm:find('Dak') then dak = id end
        if nm:find('Ignus') then ignus = id end
      end
    end
    if dak == -1 or ignus == -1 then return fail("Dak'kon and Ignus must be in the party") end
    PSTF_EvalAs(dak, 'AddXPObject(Myself,3000000)')
    return
  end
  if n < 20 then return end
  local _, _, _, _, _, _, hand, buff = PSTF_LightFx()
  if hand > hand_max then hand_max = hand end
  if buff > buff_max then buff_max = buff end
  if n == 24 then PSTF_LightShot('pst_forever/logs/lights_ignus.bmp') PSTF_LightDebug(4) end
  if n == 28 then PSTF_LightShot('pst_forever/logs/lights_ignus_off.bmp') PSTF_LightDebug(0) end
  if n == 30 then
    if hand_max == 0 then return fail('no hand light for Ignus') end
    PSTF_EvalAs(dak, 'ForceSpell(Myself,2406)')  -- Improved Strength: op353 tint + op369 glow
    cast_at = n
  end
  if cast_at > 0 and n == cast_at + 150 then
    PSTF_Note(string.format('lighting_lights: hand lights %d, buff lights %d', hand_max, buff_max))
    if buff_max == 0 then return fail('no buff colour light while Improved Strength played') end
    PSTF_Note('lighting_lights: ok')
    pstfTestTick = nil
  end
end
