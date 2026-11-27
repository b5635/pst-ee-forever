-- SPDX-License-Identifier: GPL-2.0-or-later
-- BG3 combat text ([tbc] combat_text, src/modules/tbc/combat_text.h).
-- Run: tools\launch.ps1 -Save 000000126 -Lua tests\ingame\combat_text.lua
--      -Env @{PSTF_TEST_TICK='1'; PSTF_TBC_AUTOPARTY='1'} -Until 'combat_text: ok|TEST FAIL' -Close -Timeout 400
-- 1. Blessing on the Nameless One: its name shows (status on); removed: shown again and struck through.
-- 2. Damage effects on a companion: 4 fire -> a number in the fire colour, 3 crushing -> a white number, landing
--    each glides out from the body's middle for ~0.3 s, then sits; the two stop apart (a second 1 crushing in the same
--    tick adds to the 3: "4"); a heal -> a white "+2". The engine's own "Damage: N" floaters are taken over.
-- 3. A fight against the nearest Hive thug (its AC raised: the party misses): "Miss!" or "Critical Miss!" over it,
--    weapon damage as white numbers; a line never moves while the view does not (it stays where it appeared).
-- Shots: pst_forever/logs/ctext_{status,strike,numbers,fight,hover}.bmp
local function fail(msg) PSTF_Note('TEST FAIL: ' .. msg) pstfTestTick = nil end
local FIRE, WHITE = 0x287CF6, 0xFFFFFF
local n, party, enemy, fight_at = 0, {}, -1, -1
local got = {on = false, strike = false, fire = false, white = false, apart = false, glide = false, heal = false, miss = false, wnum = false}
local shots = {}
local pos = {}  -- line key -> x, y, view centre when first seen

local function lines()
  local out = {}
  for i = 1, 24 do
    local on, x, y, a, s, k, cnt, t1, c1, t2, c2, t3, c3, t4, c4, id, slot, t0 = PSTF_CombatText(i)
    if on == 1 then
      out[#out + 1] = {x = x, y = y, a = a, s = s, k = k, n = cnt, t = t1, c = c1, t2 = t2, c2 = c2, cs = {c1, c2, c3, c4}, ts = {t1, t2, t3, t4}, id = id, slot = slot, t0 = t0}
    end
  end
  return out
end
local function shot(name)
  if shots[name] then return end
  shots[name] = true
  PSTF_LightShot('pst_forever/logs/ctext_' .. name .. '.bmp')
  PSTF_Note('combat_text: shot ' .. name .. ' at tick ' .. n)
end
local function scan()
  local vx, vy = -1, -1
  if PSTF_ViewCenter then vx, vy = PSTF_ViewCenter() end
  for _, l in ipairs(lines()) do
    if l.k == 5 then got.on = true shot('status') end
    if l.k == 6 and l.s >= 1 then got.strike = true shot('strike') end
    if l.k == 3 then
      if l.c == FIRE or l.c2 == FIRE then got.fire = true end
      if l.c == WHITE or l.c2 == WHITE then got.white = true end
      for j = 1, l.n do if l.cs[j] == WHITE and l.ts[j] == '4' then got.merged = true end end
      if fight_at > 0 and l.c == WHITE then got.wnum = true end
    end
    if l.k == 4 and l.c == WHITE and string.sub(l.t, 1, 1) == '+' then got.heal = true end
    if (l.k == 3 or l.k == 4) and fight_at < 0 then
      for _, o in ipairs(lines()) do  -- two of this creature's numbers, both settled: stopped apart
        local q = pos[o.id .. '|' .. o.t0 .. '|' .. o.c]
        local mine = pos[l.id .. '|' .. l.t0 .. '|' .. l.c]
        if o.id == l.id and (o.k == 3 or o.k == 4) and (o.t0 ~= l.t0 or o.c ~= l.c) and q and mine and n - q.t > 12
          and n - mine.t > 12 and (math.abs(o.x - l.x) > 12 or math.abs(o.y - l.y) > 14) then
          got.apart = true shot('numbers')
        end
      end
    end
    if (l.k == 2 or l.k == 1) and fight_at > 0 then
      got.miss = got.miss or l.t == 'Miss!' or l.t == 'Critical Miss!'
      if l.t == 'Miss!' or l.t == 'Critical Miss!' then shot('fight') end
      if l.t == 'Miss!' and l.k ~= 2 then return fail('Miss! is kind ' .. l.k) end
    end
    -- sitting still: the same line at the same UI point while the view stays put
    -- a number glides for kCtDriftMs (~9 ticks) first: its still check starts after that
    local num = l.k == 3 or l.k == 4
    local key = l.id .. '|' .. l.t0 .. '|' .. l.c
    local p = pos[key]
    if not p then
      pos[key] = {x = l.x, y = l.y, vx = vx, vy = vy, t = n, x0 = l.x, y0 = l.y}
    elseif num and n - p.t <= 12 then
      if p.vx == vx and p.vy == vy and (math.abs(p.x0 - l.x) > 2 or math.abs(p.y0 - l.y) > 2) then got.glide = true end
      p.x, p.y, p.vx, p.vy = l.x, l.y, vx, vy
    elseif p.vx == vx and p.vy == vy and n - p.t < 60 and (math.abs(p.x - l.x) > 1 or math.abs(p.y - l.y) > 1) then
      return fail(string.format('line "%s" moved from %d,%d to %d,%d with the view still', l.t, p.x, p.y, l.x, l.y))
    end
  end
end

function pstfTestTick()
  n = n + 1
  if n == 3 then
    for s = 0, 7 do
      local id = PSTF_PartyId(s)
      if id ~= -1 then party[#party + 1] = id end
    end
    if #party < 2 then return fail('need two party members') end
    PSTF_EvalAs(party[1], 'ApplySpellRES("SPPR101",Myself)')
    return
  end
  if n > 3 then scan() end
  if pstfTestTick == nil then return end
  if n == 40 then
    if not got.on then return fail('no status line when Blessing started') end
    PSTF_Unapply(party[1], 'SPPR101')
    return
  end
  if n == 80 then
    if not got.strike then return fail('no struck-through line when Blessing ended') end
    PSTF_StatusFxApply(party[2], 12, 4, 0x80000, 0)  -- 4 fire
    PSTF_StatusFxApply(party[2], 12, 3, 0, 0)        -- 3 crushing
    PSTF_StatusFxApply(party[2], 12, 1, 0, 0)        -- + 1 crushing in the same tick: one white "4"
    return
  end
  if n == 84 then PSTF_StatusFxApply(party[2], 17, 2, 0, 0) return end  -- heal 2
  if n == 110 then
    local cnt, floaters, taken = PSTF_CombatText()
    PSTF_Note(string.format('combat_text: lines %d, engine floaters %d, taken %d; fire %s white %s apart %s heal %s', cnt,
      floaters, taken, tostring(got.fire), tostring(got.white), tostring(got.apart), tostring(got.heal)) .. ' glide ' .. tostring(got.glide))
    if not got.fire then return fail('no fire-coloured number') end
    if not got.white then return fail('no white number for crushing damage') end
    if not got.glide then return fail('no number glided out from the body') end
    if not got.apart then return fail('the fire and crushing numbers did not stop apart') end
    if not got.heal then return fail('no white +N heal number') end
    if not got.merged then return fail('3 + 1 crushing in one tick did not show one white 4') end
    if (taken or 0) < 2 then return fail('the engine floaters were not taken over') end
    enemy = PSTF_MakeEnemyNearest()
    if enemy == -1 then return fail('no creature to fight') end
    PSTF_SetHp(enemy, 1000, 1000)  -- it lasts: the party swings many times
    PSTF_EvalAs(enemy, 'ChangeStat(Myself,ARMORCLASS,-12,SET)')
    local x, y = PSTF_SpritePos(enemy)
    C:Eval('MoveToPoint([' .. math.floor(x) .. '.' .. math.floor(y + 40) .. '])', 0)
    fight_at = n
    return
  end
  if fight_at > 0 and enemy ~= -1 and n % 30 == 0 and PSTF_HoverAt and not shots.hover then
    local x, y = PSTF_SpritePos(enemy)
    PSTF_HoverAt(math.floor(x), math.floor(y))  -- inside its selection circle
    if n > fight_at + 90 then shot('hover') PSTF_HoverAt(-1, -1) end
  end
  if fight_at > 0 and got.miss and got.wnum and shots.hover then
    PSTF_Note('combat_text: ok')
    pstfTestTick = nil
    return
  end
  if fight_at > 0 and n > fight_at + 4000 then
    return fail('fight: miss ' .. tostring(got.miss) .. ', white weapon number ' .. tostring(got.wnum))
  end
end
