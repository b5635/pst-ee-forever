-- SPDX-License-Identifier: GPL-2.0-or-later
-- statusfx in a turn-based fight: a colour pulse on a creature that is waiting for its turn (its effect clock held
-- by the round clock) must go quiet ~2 s after it started, not at that creature's next turn.
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\statusfx_tbc.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'statusfx_tbc: ok|TEST FAIL' -Close -Timeout 200
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local n, foe, put, t_put, quiet_at = 0, -1, false, 0, nil
local function pulse(list)  -- op 9 entry of the list: "9:type:parent:res:eq:param1;"
  for op, p1 in list:gmatch('(%d+):%d+:[^:]*:[^:]*:%d:(%x+);') do
    if op == '9' then return tonumber(p1, 16) end
  end
  return nil
end
function pstfTestTick()
  n = n + 1
  if n == 3 then
    foe = PSTF_MakeEnemyNearest()
    if foe == -1 then return fail('no creature to fight') end
    PSTF_SetHp(foe, 1000, 1000)
    return
  end
  if foe == -1 then return end
  local cur = PSTF_TbcCurrent()
  if not put then
    if n > 20 and cur and cur ~= -1 and cur ~= foe then
      PSTF_StatusFxApply(foe, 9, 0x0000FF00, 0x001800FF, 60)  -- red pulse, 60 s
      put, t_put = true, n
      PSTF_CenterOn(foe)
      PSTF_Note('statusfx_tbc: pulse on the waiting creature (turn of ' .. string.format('%X', cur) .. ')')
    end
    if n > 1500 then return fail('the fight never gave someone else the turn') end
    return
  end
  if n == t_put + 30 then PSTF_LightShot('pst_forever/logs/statusfx_tbc_pulse.bmp') end
  local p1 = pulse(PSTF_StatusFxEffects(foe))
  if p1 == nil and n == t_put + 1 then return fail('the test pulse did not land') end
  if p1 == 0 or p1 == nil then
    if cur == foe and not quiet_at then return fail('quieted only on its own turn') end
    if not quiet_at then
      quiet_at = n
      PSTF_Note(string.format('statusfx_tbc: pulse quiet %d ticks after it started, while %X acts', n - t_put, cur or -1))
    end
    if n == quiet_at + 30 then  -- the colour is gone from the sprite too (look at the two shots)
      PSTF_CenterOn(foe)
      PSTF_LightShot('pst_forever/logs/statusfx_tbc_quiet.bmp')
      PSTF_Note('statusfx_tbc: ok')
      pstfTestTick = nil
    end
    return
  end
  if cur == foe then return fail('its turn came and the pulse still shows (param1 ' .. string.format('%X', p1) .. ')') end
  if n > t_put + 600 then return fail('the pulse still shows after 600 ticks') end
end
