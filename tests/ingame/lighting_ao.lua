-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test: sprite AO (ao.bin darkens the creatures' ambient where the area is enclosed) and the
-- painted emitter glow (baked lights' painted flames go into the emissive buffer).
-- Run (save 076: baked emitters in view; the Hive has none): tools\launch.ps1 -Save 000000076 -Lua tests\ingame\lighting_ao.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_ao: ok|TEST FAIL' -Close -Timeout 200
-- Shots: pst_forever/logs/ao_{on,off,emissive}.bmp.
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n = 0
local on, on_px, off, off_px = -1, 0, -1, 0
local painted, ao_min, ao_seen = 0, 1, 0

function pstfTestTick()
  n = n + 1
  if n < 10 then return end
  local _, _, _, p = PSTF_LightAo(0, 0)
  if p > painted then painted = p end
  if n == 10 then
    -- the AO under every party member (raw ao at its feet)
    for i = 0, 7 do
      local id = PSTF_PartyId(i)
      if not id or id < 0 then break end
      local x, y = PSTF_SpritePos(id, 1)
      if x and x >= 0 then
        local _, ao = PSTF_LightAo(x, y)
        ao_seen = ao_seen + 1
        if ao < ao_min then ao_min = ao end
      end
    end
    PSTF_LightSet('sprite_ao', 100)
    PSTF_LightSpriteMean()
  end
  if n == 13 then on, on_px = PSTF_LightSpriteMean() PSTF_LightShot('pst_forever/logs/ao_on.bmp') PSTF_LightSet('sprite_ao', 0) end
  if n == 16 then PSTF_LightSpriteMean() end
  if n == 19 then off, off_px = PSTF_LightSpriteMean() PSTF_LightShot('pst_forever/logs/ao_off.bmp') PSTF_LightSet('sprite_ao', 60) PSTF_LightDebug(5) end
  if n == 22 then PSTF_LightShot('pst_forever/logs/ao_emissive.bmp') PSTF_LightDebug(0) end
  if n < 24 then return end
  PSTF_Note(string.format('lighting_ao: creature luma ao 100 %.2f (%d px), ao 0 %.2f (%d px), creatures %d, lowest ao %.3f, painted emitters %d',
    on, on_px, off, off_px, ao_seen, ao_min, painted))
  if on_px <= 0 or off_px <= 0 then return fail('no lit creature pixels measured') end
  if on > off + 0.01 then return fail('sprite AO made creatures brighter') end
  if ao_min < 0.95 and on >= off then return fail('a creature stands in AO ' .. ao_min .. ' but sprite_ao changed nothing') end
  if painted <= 0 then return fail('no painted emitter drawn into the emissive buffer') end
  PSTF_Note('lighting_ao: ok')
  pstfTestTick = nil
end
