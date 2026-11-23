-- SPDX-License-Identifier: GPL-2.0-or-later
-- In-game test for the gore module (material hit effects instead of the engine's blood).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\gore.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'gore: ok|TEST FAIL' -Close -Timeout 300
-- 1. Injected hits (PSTF_GoreHit) on the party pick the material from RESDATA armor + hitmat.2da: Morte BONE,
--    Dak'kon LEATHER, Ignus FIRE, the Nameless One FLESH; blood lands as stains (decals) that stay.
-- 2. A fight against the nearest Hive thug (as tbc_fight.lua: the protagonist walks up, party AI fights):
--    the engine's own AddBlood calls arrive and are swallowed.
-- Shots: pst_forever/logs/gore_{hits,stains,fight}.bmp
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local function name(id)
  local _, _, _, _, ref = PSTF_SpritePos(id, 1)
  return ref and Infinity_FetchString(ref) or ''
end
local want = {Morte = 'BONE', ["Dak'kon"] = 'LEATHER', Ignus = 'FIRE'}
local n, party, enemy, fight_at = 0, {}, -1, -1

function pstfTestTick()
  n = n + 1
  if n == 3 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then party[#party + 1] = id end
    end
    for _, id in ipairs(party) do
      local nm = name(id)
      local mat = PSTF_GoreHit(id, 3, 0)
      PSTF_Note('gore: ' .. nm .. ' -> ' .. mat)
      for key, m in pairs(want) do
        if nm:find(key, 1, true) and mat ~= m then return fail(nm .. ' is ' .. mat .. ', want ' .. m) end
      end
    end
    PSTF_GoreHit(party[1], 3, 1)  -- a critical on the protagonist: exaggerated
    return
  end
  if n == 8 then PSTF_LightShot('pst_forever/logs/gore_hits.bmp') return end
  if n == 120 then
    local parts, decals, air = PSTF_GoreState()
    PSTF_LightShot('pst_forever/logs/gore_stains.bmp')
    if decals < 20 then return fail('blood did not land as stains (' .. decals .. ' decals)') end
    if air > 40 then return fail('particles still in the air after 4 s (' .. air .. ')') end
    enemy = PSTF_MakeEnemyNearest()
    if enemy == -1 then return fail('no creature to fight') end
    local x, y = PSTF_SpritePos(enemy)
    C:Eval('MoveToPoint([' .. math.floor(x) .. '.' .. math.floor(y + 40) .. '])', 0)
    fight_at = n
    return
  end
  if fight_at < 0 then return end
  local parts, decals, air, events, swallowed, hits = PSTF_GoreState()
  if events > 0 and swallowed > 0 and n > fight_at + 30 then
    PSTF_LightShot('pst_forever/logs/gore_fight.bmp')
    PSTF_Note(string.format('gore: fight: engine blood events %d, swallowed %d, hits drawn %d, decals %d', events, swallowed, hits, decals))
    PSTF_Note('gore: ok')
    pstfTestTick = nil
    return
  end
  if n > fight_at + 6000 then return fail('no engine blood event while fighting') end
end
