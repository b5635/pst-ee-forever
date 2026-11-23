-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for lighting phase 2: the sprite buffer (creature depth maps), LM as the creatures'
-- ambient, sprite occlusion and key-light shadows.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\lighting_sprites.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_NOFIGHT='1'} -Until 'lighting_sprites: ok|TEST FAIL' -Close -Timeout 200
-- Passes when creatures are drawn into the sprite buffer with their depth frames and a plausible LM.
-- Shots: pst_forever/logs/spr_{lit,flat,buffer,noshadow}.bmp (lit, sprite_lighting off, mode 6, key shadows off).
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, best = 0, {seen = 0, depth = 0, drawn = 0, lm = -1}
local info_px, cov_px, both_px = -1, -1, -1

function pstfTestTick()
  n = n + 1
  if n < 10 then return end
  local seen, depth, drawn, frames, lm, files = PSTF_LightSprites()
  if drawn > best.drawn then best = {seen = seen, depth = depth, drawn = drawn, lm = lm} end
  if n == 11 then PSTF_LightSpriteCheck() end
  if n == 12 then info_px, cov_px, both_px = PSTF_LightSpriteCheck() PSTF_LightShot('pst_forever/logs/spr_lit.bmp') end
  if n == 13 then PSTF_LightSet('sprite_lighting', 0) end
  if n == 16 then PSTF_LightShot('pst_forever/logs/spr_flat.bmp') PSTF_LightSet('sprite_lighting', 1) end
  if n == 18 then PSTF_LightDebug(6) end
  if n == 20 then PSTF_LightShot('pst_forever/logs/spr_buffer.bmp') PSTF_LightDebug(0) PSTF_LightSet('key_shadows', 0) end
  if n == 23 then PSTF_LightShot('pst_forever/logs/spr_noshadow.bmp') PSTF_LightSet('key_shadows', 1) end
  if n < 26 then return end
  PSTF_Note(string.format('lighting_sprites: creature draws %d, with depth %d, drawn %d, mean LM %.3f, depth files %d, buffer px %d, covered %d, both %d',
    best.seen, best.depth, best.drawn, best.lm, files, info_px, cov_px, both_px))
  if files < 1000 then return fail('sprite depth files missing (' .. files .. ')') end
  if best.seen == 0 then return fail('no creature draw recorded (FX-surface creature blits)') end
  if best.depth == 0 then return fail('no creature frame found in its depth file') end
  if best.drawn < best.depth then return fail('creature frames not drawn into the sprite buffer') end
  if best.lm <= 0 or best.lm > 1.01 then return fail('LM luma out of range: ' .. best.lm) end
  if info_px <= 0 then return fail('the sprite buffer is empty') end
  if cov_px <= 0 then return fail('the coverage buffer is empty') end
  if both_px < info_px * 0.6 then return fail('sprite buffer and coverage do not line up') end
  PSTF_Note('lighting_sprites: ok')
  pstfTestTick = nil
end
